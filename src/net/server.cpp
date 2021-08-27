#include "net/server.hpp"
#include "logger.hpp"
#include "layer.hpp"

#include <cstdint>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <cstring>
#include <unistd.h>

namespace network
{
	server::~server()
	{
		if(m_thread.joinable())
			m_thread.join();
	}

	clientID server::nextClient(std::string name)
	{
		clientID id = nextClientID++;
		*::logger << CheekyLayer::logger::begin << "Accepted client " << std::dec << id << " with name " << name << CheekyLayer::logger::end;
		m_clientHandlers[id] = m_handlerFactory(id, name, this);
		return id;
	}

	void server::handleData(clientID client, serverbound::PacketType type, void* data, size_t size)
	{
		m_clientHandlers[client]->handlePacket(type, data, size);
	}

	void server::handleDisconnect(clientID client)
	{
		*::logger << CheekyLayer::logger::begin << "Lost client " << std::dec << client << CheekyLayer::logger::end;
		m_clientHandlers[client]->handleDisconnect();
		m_clientHandlers.erase(client);
	}

	basic_server::basic_server(int port, handler_factory factory) : server(factory)
	{
		m_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if(m_fd < 0)
			throw std::runtime_error("cannot create socket "+std::string(std::strerror(errno)));

		struct sockaddr_in server;
		memset(&server, 0, sizeof (server));
		server.sin_family = AF_INET;
		server.sin_addr.s_addr = htonl(INADDR_ANY);
		server.sin_port = htons(port);

		int enable = 1;
		if(setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
			throw std::runtime_error("cannot set socket options "+std::string(std::strerror(errno)));

		if(bind(m_fd, (struct sockaddr*)&server, sizeof(server)) < 0)
			throw std::runtime_error("cannot bind socket "+std::string(std::strerror(errno)));
		if(listen(m_fd, 5) < 0)
			throw std::runtime_error("cannot listen to socket "+std::string(std::strerror(errno)));

		m_thread = std::thread(&basic_server::acceptingThread, this, std::shared_future<void>(m_exit.get_future()));
	}

	basic_server::~basic_server()
	{
		m_exit.set_value();

		for(auto& [a, thread] : m_receivingThreads) thread.join();
		m_thread.join();

		for(auto [a, fd] : m_clientSockets) close(fd);

		close(m_fd);
	}

	void basic_server::send(clientID client, clientbound::PacketType type, void *data, size_t size)
	{
		int fd = m_clientSockets[client];

		clientbound::BasicHeader header{.type = type, .size = size};
		::send(fd, &header, sizeof(header), 0);
		::send(fd, data, size, 0);
	}

	void basic_server::disconnect(clientID client)
	{
		handleDisconnect(client);
		close(m_clientSockets[client]);
		m_clientSockets.erase(client);
	}

	void basic_server::acceptingThread(std::shared_future<void> exit)
	{
		struct pollfd pfd = { m_fd, POLLIN, 0 };
		struct sockaddr_in clientAddr;
		socklen_t len = sizeof(clientAddr);

		while(true)
		{
			if(poll(&pfd, 1, 100) > 0)
			{
				int fd = accept(m_fd, (struct sockaddr*)&clientAddr, &len);
				if(fd < 0)
					return;
				
				std::string name = std::string(inet_ntoa(clientAddr.sin_addr))+":"+std::to_string(ntohs(clientAddr.sin_port));
				clientID client = nextClient(name);
				m_clientSockets[client] = fd;
				m_receivingThreads[client] = std::thread(&basic_server::receivingThread, this, client, exit);
			}
			else
			{
				if(exit.wait_for(std::chrono::milliseconds(1)) == std::future_status::ready)
					return;
			}
		}
	}

	void basic_server::receivingThread(clientID client, std::shared_future<void> exit)
	{
		int fd = m_clientSockets[client];
		while(true)
		{
			struct pollfd pfd = { fd, POLLIN, 0 };
			if(poll(&pfd, 1, 100) > 0)
			{
				if(pfd.revents & POLLERR)
				{
					handleDisconnect(client);
					close(fd);
					m_clientSockets.erase(client);
					return;
				}

				serverbound::BasicHeader header{};
				size_t len;
				if((len=recv(fd, &header, sizeof(serverbound::BasicHeader), 0)) == sizeof(serverbound::BasicHeader))
				{
					*::logger << CheekyLayer::logger::begin << header.type << " " << header.size << CheekyLayer::logger::end;
					if(header.size > 0)
					{
						uint8_t* buffer = new uint8_t[header.size];
						size_t n = recv(fd, buffer, header.size, 0);
						handleData(client, header.type, buffer, n);
						delete [] buffer;
					}
					else
					{
						handleData(client, header.type, nullptr, 0);
					}
				}
				else if(len <= 0)
				{
					handleDisconnect(client);
					close(fd);
					m_clientSockets.erase(client);
					return;
				}
			}
			else
			{
				if(exit.wait_for(std::chrono::milliseconds(1)) == std::future_status::ready)
				{
					handleDisconnect(client);
					close(fd);
					m_clientSockets.erase(client);
					return;
				}
			}
		}
	}
}
