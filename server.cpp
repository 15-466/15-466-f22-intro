
#include "Connection.hpp"

#include "hex_dump.hpp"

#include "Game.hpp"

#include <chrono>
#include <stdexcept>
#include <iostream>
#include <cassert>
#include <unordered_map>

#ifdef _WIN32
extern "C" { uint32_t GetACP(); }
#endif
int main(int argc, char **argv) {
#ifdef _WIN32
	{ //when compiled on windows, check that code page is forced to utf-8 (makes file loading/saving work right):
		//see: https://docs.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page
		uint32_t code_page = GetACP();
		if (code_page == 65001) {
			std::cout << "Code page is properly set to UTF-8." << std::endl;
		} else {
			std::cout << "WARNING: code page is set to " << code_page << " instead of 65001 (UTF-8). Some file handling functions may fail." << std::endl;
		}
	}

	//when compiled on windows, unhandled exceptions don't have their message printed, which can make debugging simple issues difficult.
	try {
#endif

	//------------ argument parsing ------------

	if (argc != 2) {
		std::cerr << "Usage:\n\t./server <port>" << std::endl;
		return 1;
	}

	//------------ initialization ------------

	Server server(argv[1]);

	//------------ main loop ------------

	//keep track of which connection is controlling which player:
	std::unordered_map< Connection *, Player * > connection_to_player;
	//keep track of game state:
	Game game;

	while (true) {
		static auto next_tick = std::chrono::steady_clock::now() + std::chrono::duration< double >(Game::Tick);
		//process incoming data from clients until a tick has elapsed:
		while (true) {
			auto now = std::chrono::steady_clock::now();
			double remain = std::chrono::duration< double >(next_tick - now).count();
			if (remain < 0.0) {
				next_tick += std::chrono::duration< double >(Game::Tick);
				break;
			}

			//helper used on client close (due to quit) and server close (due to error):
			auto remove_connection = [&](Connection *c) {
				auto f = connection_to_player.find(c);
				assert(f != connection_to_player.end());
				game.remove_player(f->second);
				connection_to_player.erase(f);
			};

			server.poll([&](Connection *c, Connection::Event evt){
				if (evt == Connection::OnOpen) {
					//client connected:

					//create some player info for them:
					connection_to_player.emplace(c, game.spawn_player());

				} else if (evt == Connection::OnClose) {
					//client disconnected:

					remove_connection(c);

				} else { assert(evt == Connection::OnRecv);
					//got data from client:
					//std::cout << "current buffer:\n" << hex_dump(c->recv_buffer); std::cout.flush(); //DEBUG

					//look up in players list:
					auto f = connection_to_player.find(c);
					assert(f != connection_to_player.end());
					Player &player = *f->second;

					//handle messages from client:
					try {
						bool handled_message;
						do {
							handled_message = false;
							if (player.controls.recv_controls_message(c)) handled_message = true;
							//TODO: extend for more message types as needed
						} while (handled_message);
					} catch (std::exception const &e) {
						std::cout << "Disconnecting client:" << e.what() << std::endl;
						c->close();
						remove_connection(c);
					}
				}
			}, remain);
		}

		//update current game state
		game.update(Game::Tick);

		//send updated game state to all clients
		for (auto &[c, player] : connection_to_player) {
			game.send_state_message(c, player);
		}

	}


	return 0;

#ifdef _WIN32
	} catch (std::exception const &e) {
		std::cerr << "Unhandled exception:\n" << e.what() << std::endl;
		return 1;
	} catch (...) {
		std::cerr << "Unhandled exception (unknown type)." << std::endl;
		throw;
	}
#endif
}
