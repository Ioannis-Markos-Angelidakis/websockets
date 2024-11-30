#include <crow_all.h>
#include <deque>
#include <unordered_map>
#include <mutex>
#include <future>
#include <algorithm>

const std::string IP{""};

int32_t main() {
    crow::SimpleApp app;

    // Shared resources
    std::deque<std::string> chat_history; // Store chat messages
    std::unordered_map<crow::websocket::connection*, std::string> clients; // Track clients and usernames
    std::mutex resource_mutex; // Mutex for thread-safe access

    const size_t max_history_size = 100; // Maximum messages in history

    CROW_ROUTE(app, "/") ([](const crow::request& request) {
        crow::mustache::template_t page = crow::mustache::load("chat.html");

        return page.render();
    });

	CROW_ROUTE(app, "/chat")
	.websocket(&app)
	.onopen([&](crow::websocket::connection& conn) {
		std::unique_lock lock(resource_mutex);
		clients[&conn] = "Anonymous";
		std::cout << "New client connected. Total clients: " << clients.size() << std::endl;

		// Send chat history to the new client
		for (const auto& message : chat_history) {
			conn.send_text(message);
		}
	})
	.onmessage([&](crow::websocket::connection& conn, const std::string& data, bool is_binary) {
		std::unique_lock lock(resource_mutex);

		if (data.starts_with("/name ")) {
			std::string new_username = data.substr(6);
			if (!new_username.empty()) {
				std::string old_username = clients[&conn];
				clients[&conn] = new_username;
				std::string notification = old_username + " is now known as " + new_username;
				chat_history.push_back(notification);

				if (chat_history.size() > max_history_size) {
					chat_history.pop_front();
				}

				for (auto& [client, username] : clients) {
					client->send_text(notification);
				}
			}
			return;
		}

		// Handle typing notification
		if (data == "/typing") {
			std::string typing_user = clients[&conn];
			for (auto& [client, username] : clients) {
				if (client != &conn) {
					client->send_text(typing_user + " is typing...");
				}
			}
			return;
		}

		// Handle stopped typing notification
		if (data == "/stopped") {
			std::string typing_user = clients[&conn];
			for (auto& [client, username] : clients) {
				if (client != &conn) {
					client->send_text(typing_user + " stopped typing.");
				}
			}
			return;
		}

		// Normal message broadcast
		std::string message = clients[&conn] + ": " + data;
		chat_history.push_back(message);
		if (chat_history.size() > max_history_size) {
			chat_history.pop_front();
		}

		// Send the message to other clients
		for (auto& [client, username] : clients) {
			if (client != &conn) { // Skip the sender
				client->send_text(message);
			}
		}
	})
	.onclose([&](crow::websocket::connection& conn, const std::string& reason) {
		std::unique_lock lock(resource_mutex);
		std::string username = clients[&conn];
		clients.erase(&conn);

		std::string notification = username + " has left the chat.";
		chat_history.push_back(notification);
		if (chat_history.size() > max_history_size) {
			chat_history.pop_front();
		}

		for (auto& [client, username] : clients) {
			client->send_text(notification);
		}

		std::cout << "Client disconnected. Remaining clients: " << clients.size() << std::endl;
	});

    std::future<void> _a = app.bindaddr(IP).port(18080).multithreaded().run_async();
}
