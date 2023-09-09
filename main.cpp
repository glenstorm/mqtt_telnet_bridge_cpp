#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <boost/asio.hpp>
#include <mqtt/async_client.h>

#include <boost/uuid/uuid.hpp>	           // uuid class
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.
#include <boost/lexical_cast.hpp>

using namespace std;
using namespace boost::asio;
using namespace std::chrono;
using ip::tcp;

const std::string SERVER_ADDRESS("localhost");

class MyTCPHandler
{
public:
	MyTCPHandler(MyTCPHandler&& e) noexcept
		:socket(move(e.socket))
		,startPoll(move(e.startPoll))
		,client(move(e.client))
	{
		std::cerr << "move" << std::endl;
	}

	MyTCPHandler() = delete;
	void operator=(const MyTCPHandler&) = delete;
	MyTCPHandler(const MyTCPHandler& e) = delete;

	MyTCPHandler(const shared_ptr<tcp::socket> &_socket)
		:socket(_socket)
		,startPoll(false)
	{
		Init();
	}

	void Init()
	{
		std::cerr << "MyTCPHandler" << std::endl;
		try {
			boost::uuids::uuid uuid = boost::uuids::random_generator()();
			client = std::make_shared<mqtt::async_client>(SERVER_ADDRESS, boost::lexical_cast<std::string>(uuid));

			auto conn_opts = mqtt::connect_options_builder()
					.keep_alive_interval(seconds(30))
					.clean_session(true)
					.finalize();


			client->set_message_callback([this](mqtt::const_message_ptr msg) {
				boost::asio::streambuf buffer;
				std::ostream os(&buffer);
				if(msg && startPoll)
					os << msg->get_topic() << ": " << msg->to_string() << std::endl;

				{
					std::lock_guard<std::mutex> lock(socketMutex);
					boost::asio::write(*socket, buffer);
				}
			});

			client->connect(conn_opts);

			client->start_consuming();

		} catch (const mqtt::exception &e) {
			std::cerr << "MQTT error: " << e.what() << std::endl;
			client->stop_consuming();
			client->disconnect();
		}
	}

		~MyTCPHandler()
	{
		std::cerr << "~MyTCPHandler" << std::endl;
		client->stop_consuming();
		client->disconnect();
	}

	void operator()()
	{
		try {
			boost::asio::streambuf buffer;
			std::ostream os(&buffer);

			os << "Добро пожаловать в MQTT-to-Telnet мост!\n";
			os << "Для подписки на топики, введите 'subscribe <topic>'.\n";
			os << "Для начала приема сообщений, введите 'poll'.\n";
			os << "Для выхода, введите 'exit'.\n";
			os << "Введите команду: ";
			{
				std::lock_guard<std::mutex> lock(socketMutex);
				boost::asio::write(*socket, buffer);
			}
			std::string subscription;

			while (true) {
				boost::asio::streambuf inputBuffer;
				boost::asio::read_until(*socket, inputBuffer, '\n');

				std::istream is(&inputBuffer);
				std::string command;
				is >> command;
				cout << command << endl;


				if(startPoll) // block any input after poll
					continue;

				if (command == "subscribe") {
					is >> subscription;
					client->subscribe(subscription, 1);
					os << "Подписка на " << subscription << " для клиента " << client->get_client_id() <<  " выполнена.\n";
				} else if (command == "poll") {
					startPoll = true;
					os << "Начинаем прием сообщений для клиента: " << client->get_client_id() << "\n";
				} else if (command == "exit") {
					os << "До свидания!\n";
					break;
				} else {
					os << "Неизвестная команда. Попробуйте снова.\n";
				}

				// flush accumulated messages
				if (startPoll) {
					mqtt::const_message_ptr msg;
					while(client->try_consume_message(&msg))
						os << msg->get_topic() <<  ": " << msg->get_payload_str() << endl;

				}
				else
					os << "Введите команду: ";

				{
					std::lock_guard<std::mutex> lock(socketMutex);
					boost::asio::write(*socket, buffer);
				}
			}
		} catch (const std::exception &e) {
			std::cerr << "Ошибка: " << e.what() << std::endl;
		}
	}

private:
	bool startPoll;
	mqtt::async_client_ptr client;
	std::shared_ptr<tcp::socket> socket;
	std::mutex socketMutex;
};


void thread_run(MyTCPHandler& hndl)
{
	hndl();
}

int main() {
	try {
		vector<MyTCPHandler> handlers;
		handlers.reserve(1000);

		// Создание и настройка io_context
		io_context io;
		tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), 1234));

		std::cout << "Сервер запущен и слушает на порту 1234\n";

		while (true) {
			auto socket = make_shared<tcp::socket>(io);
			acceptor.accept(*socket);

			handlers.emplace_back(socket);
			std::thread(&thread_run, ref(handlers.back())).detach();
		}
	} catch (const std::exception &e) {
		std::cerr << "Ошибка: " << e.what() << std::endl;
	}

	return 0;
}
