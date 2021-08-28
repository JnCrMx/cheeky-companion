#include <SDL.h>
#include <SDL_error.h>
#include <SDL_events.h>
#include <SDL_gamecontroller.h>
#include <SDL_haptic.h>
#include <SDL_joystick.h>

#include <chrono>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <boost/program_options.hpp>
#include <boost/program_options/errors.hpp>
#include <boost/program_options/option.hpp>

#include <cstdint>
#include <string>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <future>

#include "packets.hpp"

namespace po = boost::program_options;
using namespace network;

void sendPacket(int socket, serverbound::PacketType type, void *data, size_t size)
{
	serverbound::BasicHeader header{.type = type, .size = size};
	::send(socket, &header, sizeof(header), 0);
	::send(socket, data, size, 0);
}

int main(int argc, char* argv[])
{
	std::string hostname;
	int port;
	std::string controllerName;

	std::string playerName = "anonymous";
	std::string companion;

	po::options_description options("Options");
    options.add_options()("help", "print this help message");
    options.add_options()("server", po::value<std::string>(&hostname)->value_name("hostname")->required(), "server to connect to");
    options.add_options()("port", po::value<int>(&port)->value_name("port")->required(), "port to connect to");
    options.add_options()("controller", po::value<std::string>(&controllerName)->value_name("name"), "controller to use");
    options.add_options()("list_controllers", "list connected controllers");
	options.add_options()("playername", po::value<std::string>(&playerName)->value_name("playername"), "your name");
	options.add_options()("companion", po::value<std::string>(&companion)->value_name("companion")->required(), "companion to use");
	
	po::variables_map vm;
	try 
	{
		po::store(po::parse_command_line(argc, argv, options), vm);
		po::notify(vm);
	}
	catch(const po::error& err)
	{
		if(!vm.count("help") && !vm.count("list_controllers"))
		{
			std::cout << err.what() << std::endl;
    		std::cout << options << std::endl;
    		return 2;
		}
	}
	if(vm.count("help"))
	{
    	std::cout << options << std::endl;
    	return 0;
	}

	if(SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC | SDL_INIT_GAMECONTROLLER) < 0)
		throw std::runtime_error("failed to initialize SDL");
	
	int joystickCount = SDL_NumJoysticks();
	if(joystickCount < 1)
	{
		std::cerr << "No joysticks found!" << std::endl;
		return 2;
	}
	if(vm.count("list_controllers"))
	{
		for(int i=0; i<joystickCount; i++)
		{
			if(SDL_IsGameController(i))
			{
				std::cout << "\"" << SDL_GameControllerNameForIndex(i) << "\" (game controller)" << std::endl;
			}
			else
			{
				std::cout << "\"" << SDL_JoystickNameForIndex(i) << "\" (joystick only)" << std::endl;
			}
		}
		return 0;
	}

	int joystickIndex = -1;
	bool gameController = false;
	for(int i=0; i<joystickCount; i++)
	{
		bool controller = SDL_IsGameController(i);
		std::string name = controller ? SDL_GameControllerNameForIndex(i) : SDL_JoystickNameForIndex(i);
		if(vm.count("controller") && name != controllerName)
			continue;
		if((controller && !gameController) || joystickIndex == -1)
		{
			joystickIndex = i;
			gameController = true;
		}
	}
	if(joystickIndex == -1)
	{
		std::cerr << "No useable joystick or game controller found. Check if the name you provided is correct." << std::endl;
		return 2;
	}
	if(!gameController)
	{
		std::cerr << "Currently only game controllers are supported :(" << std::endl;
		return 2;
	}

	std::string name = gameController ? SDL_GameControllerNameForIndex(joystickIndex) : SDL_JoystickNameForIndex(joystickIndex);
	std::cout << "Using " << (gameController?"game controller":"joystick") << " " << joystickIndex << " \"" << name << "\"" << std::endl;

	SDL_GameController* controller = SDL_GameControllerOpen(joystickIndex);
	if(!controller)
	{
		std::cerr << "Failed to open controller " << joystickIndex << ": " << SDL_GetError() << std::endl;
		return 2;
	}

	SDL_Haptic* haptic = SDL_HapticOpenFromJoystick(SDL_GameControllerGetJoystick(controller));
	if(haptic && SDL_HapticRumbleSupported(haptic))
	{
		std::cout << "Controller has rumble support" << std::endl;
		if(SDL_HapticRumbleInit(haptic) < 0)
		{
			std::cerr << "Rumble initialization failed: " << SDL_GetError() << std::endl;
		}
	}

	int socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(socket < 0) throw std::runtime_error("cannot create socket "+std::string(std::strerror(errno)));

	struct hostent* hp = ::gethostbyname(hostname.c_str());
	if(!hp)
	{
		std::cerr << "Unknown host: " << hostname << std::endl;
		close(socket);
		return 2;
	}
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	::bcopy(hp->h_addr, &addr.sin_addr, hp->h_length);
	addr.sin_port = htons(port);
	if(connect(socket, (sockaddr*)&addr, sizeof(struct sockaddr_in)) < 0)
	{
		std::cerr << "Failed to connect to " << hostname << ":" << port << std::endl;
		close(socket);
		return 2;
	}

	serverbound::JoinPacket join{};
	strncpy(join.name, playerName.c_str(), sizeof(join.name));
	strncpy(join.companion, companion.c_str(), sizeof(join.name));
	sendPacket(socket, serverbound::PacketType::Join, &join, sizeof(join));

	std::promise<void> exitPromise;
	std::shared_future<void> exitFuture = exitPromise.get_future();

	struct {
		int16_t dx = 0;
		int16_t dy = 0;
		int16_t dz = 0;

		int16_t rx = 0;
		int16_t ry = 0;
	} rawData{};
	struct {
		float yaw = 0.0f;
	} playerState{};

	std::thread t([&exitFuture, &rawData, &playerState, socket](){
		for(;;)
		{
			if(rawData.dx != 0 || rawData.dy != 0 || rawData.dz != 0)
			{
				float ndx = rawData.dx/((float)INT16_MAX);
				float ndy = rawData.dy/((float)INT16_MAX);
				float ndz = rawData.dz/((float)INT16_MAX);

				float rdx = -ndy * std::cos(playerState.yaw) - ndx * std::sin(playerState.yaw);
				float rdy = -ndy * std::sin(playerState.yaw) + ndx * std::cos(playerState.yaw);

				serverbound::MovePacket move = { .dx = 0.01f*rdx, .dy = 0.01f*ndz, .dz = 0.01f*rdy};
				sendPacket(socket, serverbound::PacketType::Move, &move, sizeof(move));
			}
			if(rawData.rx != 0 || rawData.ry != 0)
			{
				float nrx = rawData.rx/((float)INT16_MAX);
				float nry = rawData.ry/((float)INT16_MAX);

				playerState.yaw += nrx*0.01f;
				serverbound::RotatePacket rotate = {.yaw = playerState.yaw};
				sendPacket(socket, serverbound::PacketType::Rotate, &rotate, sizeof(rotate));
			}

			if(exitFuture.wait_for(std::chrono::milliseconds(1)) == std::future_status::ready)
				return;
		}
	});

	SDL_Event event;
	bool done = false;
	while((!done) && (SDL_WaitEvent(&event)))
	{
		switch(event.type)
		{
			case SDL_CONTROLLERBUTTONDOWN:
				if(event.cbutton.button == SDL_GameControllerButton::SDL_CONTROLLER_BUTTON_BACK)
				{
					serverbound::TeleportPacket teleport = {.target = serverbound::TeleportTarget::Origin};
					sendPacket(socket, serverbound::PacketType::Teleport, &teleport, sizeof(teleport));
				}

				SDL_HapticRumblePlay(haptic, 1.0, 100);
				std::cout << SDL_GameControllerGetStringForButton((SDL_GameControllerButton)event.cbutton.button) << ": " << (int)event.cbutton.state << std::endl;
				break;
			case SDL_CONTROLLERBUTTONUP:
				SDL_HapticRumbleStop(haptic);
				std::cout << SDL_GameControllerGetStringForButton((SDL_GameControllerButton)event.cbutton.button) << ": " << (int)event.cbutton.state << std::endl;
				break;
			case SDL_CONTROLLERAXISMOTION:
				std::cout << SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)event.caxis.axis) << ": " << (int)event.caxis.value << std::endl;
				if(event.caxis.axis == SDL_GameControllerAxis::SDL_CONTROLLER_AXIS_LEFTX)
					rawData.dx = event.caxis.value;
				if(event.caxis.axis == SDL_GameControllerAxis::SDL_CONTROLLER_AXIS_LEFTY)
					rawData.dy = event.caxis.value;
				if(event.caxis.axis == SDL_GameControllerAxis::SDL_CONTROLLER_AXIS_TRIGGERLEFT)
					rawData.dz = -event.caxis.value;
				if(event.caxis.axis == SDL_GameControllerAxis::SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
					rawData.dz = event.caxis.value;
					
				if(event.caxis.axis == SDL_GameControllerAxis::SDL_CONTROLLER_AXIS_RIGHTX)
					rawData.rx = event.caxis.value;
				if(event.caxis.axis == SDL_GameControllerAxis::SDL_CONTROLLER_AXIS_RIGHTY)
					rawData.ry = event.caxis.value;
				break;
			case SDL_QUIT:
				done = true;
				break;
		}
	}

	exitPromise.set_value();
	t.join();

	close(socket);

	return 0;
}
