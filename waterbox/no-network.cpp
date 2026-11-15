/* no-network.cpp - the engine's sockets, answered by the core: there are none.
 *
 * Sonic 3 A.I.R. talks to its game server (the update check, the ghost sync
 * and the other online features), to netplay partners and to Crowd Control
 * through oxygen_netcore's Sockets.cpp. The core builds this file in its
 * place (sources.mk): every socket is invalid, every connection, bind and
 * send fails, and no name is ever resolved. So nothing in the machine can
 * reach a network, and the engine's own code sees what it sees on a computer
 * with no connection at all.
 *
 * The class layout is upstream's (oxygen_netcore/network/Sockets.h); only the
 * bodies are the core's.
 */
#include "oxygen_netcore/pch.h"
#include "oxygen_netcore/network/Sockets.h"

void Sockets::startupSockets() { mIsInitialized = true; }
void Sockets::shutdownSockets() { mIsInitialized = false; }
bool Sockets::resolveToIP(const std::string&, std::string& outIP, bool) { outIP.clear(); return false; }

bool SocketAddress::operator==(const SocketAddress& other) const
{
	return getIP() == other.getIP() && getPort() == other.getPort();
}
std::string SocketAddress::toLoggedString() const { return toString(); }
void SocketAddress::assureSockAddr() const { memset(mSockAddr, 0, sizeof mSockAddr); mHasSockAddr = true; }
void SocketAddress::assureIpPort() const { mHasIpPort = true; }
void SocketAddress::assureAddrHash() const
{
	if (mHasAddrHash) return;
	mAddrHash = rmx::getMurmur2_64(String(mIP)) ^ mPort;
	mHasAddrHash = true;
}

struct TCPSocket::Internal { SocketAddress mRemoteAddress; };
TCPSocket::TCPSocket() {}
TCPSocket::~TCPSocket() { delete mInternal; }
bool TCPSocket::isValid() const { return false; }
void TCPSocket::close() {}
const SocketAddress& TCPSocket::getRemoteAddress()
{
	static SocketAddress none;
	return none;
}
void TCPSocket::swapWith(TCPSocket& other) { std::swap(mInternal, other.mInternal); }
bool TCPSocket::setupServer(uint16, Sockets::ProtocolFamily) { return false; }
bool TCPSocket::acceptConnection(TCPSocket&) { return false; }
bool TCPSocket::connectTo(const std::string&, uint16, Sockets::ProtocolFamily) { return false; }
bool TCPSocket::sendData(const uint8*, size_t) { return false; }
bool TCPSocket::sendData(const std::vector<uint8>&) { return false; }
bool TCPSocket::receiveBlocking(ReceiveResult& r) { r.mBuffer.clear(); return false; }
bool TCPSocket::receiveNonBlocking(ReceiveResult& r) { r.mBuffer.clear(); return false; }
bool TCPSocket::receiveInternal(ReceiveResult& r) { r.mBuffer.clear(); return false; }

struct UDPSocket::Internal { int unused; };
UDPSocket::~UDPSocket() { delete mInternal; }
bool UDPSocket::isValid() const { return false; }
void UDPSocket::close() {}
bool UDPSocket::bindToPort(uint16, Sockets::ProtocolFamily) { return false; }
bool UDPSocket::bindToAnyPort(Sockets::ProtocolFamily) { return false; }
bool UDPSocket::sendData(const uint8*, size_t, const SocketAddress&) { return false; }
bool UDPSocket::sendData(const std::vector<uint8>&, const SocketAddress&) { return false; }
bool UDPSocket::receiveBlocking(ReceiveResult& r) { r.mBuffer.clear(); return false; }
bool UDPSocket::receiveNonBlocking(ReceiveResult& r) { r.mBuffer.clear(); return false; }
bool UDPSocket::receiveInternal(ReceiveResult& r) { r.mBuffer.clear(); return false; }
