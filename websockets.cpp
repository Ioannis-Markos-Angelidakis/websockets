#include <crow_all.h>
#include <deque>
#include <unordered_map>
#include <mutex>
#include <future>

const std::string IP{""};

void broadcastClientCount(const std::unordered_map<crow::websocket::connection*, std::string>& clients) {
    std::string user_count_message = "Users connected: " + std::to_string(clients.size());
    for (auto& [client, username] : clients) {
        client->send_text(user_count_message);
    }
}

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
		std::cout << "New client connected. Total clients: " << clients.size() << "\n"; 

		// Send chat history to the new client
		for (const std::string& message : chat_history) {
			conn.send_text(message);
		}

		// Broadcast updated client count
		broadcastClientCount(clients);
	})
	.onmessage([&](crow::websocket::connection& conn, const std::string& data, bool is_binary) {
		std::unique_lock lock(resource_mutex);
		
		if (data.starts_with("/name ")) {
			std::string new_username = data.substr(6);

			for (auto& [client, username] : clients) {
				if (username == new_username && (client != &conn)) {
					conn.send_text("/error usarname exists!");
					if (clients[&conn].empty()) {
						clients[&conn] = "Anonymous";
					}
					return;
				} 
			}

			if (!new_username.empty()) {
				std::string old_username = clients[&conn];
				clients[&conn] = new_username;

				// Notify others that the old user has stopped typing
				for (auto& [client, username] : clients) {
					if (client != &conn) {
						client->send_text(old_username + " stopped typing.");
					}
				}
				conn.send_text("You are now known as " + new_username);
				// Broadcast the name change
				std::string notification = old_username + " is now known as " + new_username;

				for (auto& [client, username] : clients) {
					if (client != &conn) {
						client->send_text(notification);
					}
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
		chat_history.emplace_back(message);
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
		chat_history.emplace_back(notification);
		if (chat_history.size() > max_history_size) {
			chat_history.pop_front();
		}

		// Notify all clients of the disconnection
		for (auto& [client, username] : clients) {
			client->send_text(notification);
		}

		// Broadcast updated client count
		broadcastClientCount(clients);

		std::cout << "Client disconnected. Remaining clients: " << clients.size() << std::endl;
	});

    std::future<void> _a = app.bindaddr(IP).port(18080).multithreaded().run_async();
}
