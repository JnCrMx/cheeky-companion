#pragma once

#include <cstddef>
#include <cstdint>

namespace network
{
	using clientID = int;

	namespace serverbound
	{
		enum PacketType : uint32_t
		{
			Join,
			Leave,
			Move,
			Rotate,
			Look,
			Teleport
		};

		struct __attribute__((packed)) BasicHeader
		{
			PacketType type;
			size_t size;
		};

		struct __attribute__((packed)) JoinPacket
		{
			char name[64];
			char companion[64];
		};

		struct __attribute__((packed)) LeavePacket
		{

		};

		struct __attribute__((packed)) MovePacket
		{
			float dx;
			float dy;
			float dz;
		};

		struct __attribute__((packed)) RotatePacket
		{
			float yaw;
		};

		struct __attribute__((packed)) LookPacket
		{
			float yaw;
			float pitch;
		};

		enum TeleportTarget : uint8_t
		{
			Player
		};

		struct __attribute__((packed)) TeleportPacket
		{
			TeleportTarget target;
		};
	}

	namespace clientbound
	{
		enum PacketType : uint32_t
		{
			Disconnect
		};

		struct __attribute__((packed)) BasicHeader
		{
			PacketType type;
			size_t size;
		};
		
		enum DisconnectReason : uint32_t
		{
			Generic,
			JoinDenied,
			UnknownCompanion,
			TooManyClients,
			Kicked
		};

		struct __attribute__((packed)) DisconnectPacket
		{
			DisconnectReason reason;
		};
	}
}
