/*******************************************************************************
 *  Copyright 2012 MaidSafe.net limited                                        *
 *                                                                             *
 *  The following source code is property of MaidSafe.net limited and is not   *
 *  meant for external use.  The use of this code is governed by the licence   *
 *  file licence.txt found in the root of this directory and also on           *
 *  www.maidsafe.net.                                                          *
 *                                                                             *
 *  You are not free to copy, amend or otherwise use this source code without  *
 *  the explicit written permission of the board of directors of MaidSafe.net. *
 ******************************************************************************/
// Original author: Christopher M. Kohlhoff (chris at kohlhoff dot com)

#include "maidsafe/rudp/transport.h"

#include <cassert>
#include <functional>

#include "maidsafe/rudp/connection.h"
#include "maidsafe/rudp/log.h"
#include "maidsafe/rudp/managed_connections.h"
#include "maidsafe/rudp/utils.h"
#include "maidsafe/rudp/core/acceptor.h"
#include "maidsafe/rudp/core/multiplexer.h"
#include "maidsafe/rudp/core/socket.h"

namespace asio = boost::asio;
namespace ip = asio::ip;
namespace args = std::placeholders;

namespace maidsafe {

namespace rudp {

Transport::Transport(asio::io_service &asio_service)   // NOLINT
    : strand_(asio_service),
      multiplexer_(new detail::Multiplexer(asio_service)),
      acceptor_(),
      connections_(),
      this_endpoint_() {}

Transport::~Transport() {
  for (auto it = connections_.begin(); it != connections_.end(); ++it)
    (*it)->Close();
}

//ReturnCode Transport::StartListening(const Endpoint &endpoint) {
//  if (listening_port_ != 0)
//    return kAlreadyStarted;
//
//  ip::udp::endpoint ep(endpoint.ip, endpoint.port);
//  ReturnCode condition = multiplexer_->Open(ep);
//  if (condition != kSuccess)
//    return condition;
//
//  acceptor_.reset(new Acceptor(*multiplexer_));
//  listening_port_ = endpoint.port;
//  transport_details_.endpoint.port = listening_port_;
//  transport_details_.endpoint.ip = endpoint.ip;
//
//  StartAccept();
//  StartDispatch();
//
//  return kSuccess;
//}
//
//void Transport::StopListening() {
//  if (acceptor_)
//    strand_.dispatch(std::bind(&Transport::CloseAcceptor, acceptor_));
//  if (multiplexer_)
//    strand_.dispatch(std::bind(&Transport::CloseMultiplexer, multiplexer_));
//  listening_port_ = 0;
//  acceptor_.reset();
//  multiplexer_.reset(new Multiplexer(asio_service_));
//}

Endpoint Transport::Bootstrap(
    const std::vector<Endpoint> &bootstrap_endpoints) {
  BOOST_ASSERT(!multiplexer_->IsOpen());
  ReturnCode result = multiplexer_->Open(ip::udp::v4());
  if (result != kSuccess) {
    DLOG(ERROR) << "Failed to open multiplexer.";
    return Endpoint();
  }

  StartDispatch();

  for (auto itr(bootstrap_endpoints.begin());
       itr != bootstrap_endpoints.end();
       ++itr) {
    ConnectionPtr connection(std::make_shared<Connection>(shared_from_this(),
                                                          strand_,
                                                          multiplexer_, *itr));
    if (IsValid(this_endpoint_)) {
      DoInsertConnection(connection);
      break;
    }
  }

  return this_endpoint_;
}

void Transport::RendezvousConnect(const Endpoint &/*peer_endpoint*/,
                                  const std::string &/*validation_data*/) {
}

int Transport::CloseConnection(const Endpoint &/*peer_endpoint*/) {
  return 0;
}

int Transport::Send(const Endpoint &/*peer_endpoint*/,
                    const std::string &/*message*/) const {
//  strand_.dispatch(std::bind(&Transport::DoSend, shared_from_this(), data,
//                             endpoint, timeout));
                      return 0;
}

Endpoint Transport::this_endpoint() const {
  return this_endpoint_;
}

size_t Transport::ConnectionsCount() const {
  // TODO(Fraser#5#): 2012-04-03 - Handle thread-safety
  return connections_.size();
}

void Transport::CloseAcceptor(AcceptorPtr acceptor) {
  acceptor->Close();
}

void Transport::CloseMultiplexer(MultiplexerPtr multiplexer) {
  multiplexer->Close();
}

void Transport::StartDispatch() {
  auto handler = strand_.wrap(std::bind(&Transport::HandleDispatch,
                                        shared_from_this(),
                                        multiplexer_, args::_1));
  multiplexer_->AsyncDispatch(handler);
}

void Transport::HandleDispatch(MultiplexerPtr multiplexer,
                               const boost::system::error_code &/*ec*/) {
  if (!multiplexer->IsOpen())
    return;

  StartDispatch();
}

//void Transport::StartAccept() {
//  ip::udp::endpoint endpoint;  // Endpoint is assigned when socket is accepted.
//  ConnectionPtr connection(std::make_shared<Connection>(shared_from_this(),
//                                                        strand_,
//                                                        multiplexer_,
//                                                        endpoint));
//
//  acceptor_->AsyncAccept(connection->Socket(),
//                         strand_.wrap(std::bind(&Transport::HandleAccept,
//                                                shared_from_this(), acceptor_,
//                                                connection, args::_1)));
//}
//
//void Transport::HandleAccept(AcceptorPtr acceptor,
//                             ConnectionPtr connection,
//                             const boost::system::error_code &ec) {
//  if (!acceptor->IsOpen())
//    return;
//
//  if (!ec) {
//    // It is safe to call DoInsertConnection directly because HandleAccept() is
//    // already being called inside the strand.
//    DoInsertConnection(connection);
//    connection->StartReceiving();
//  }
//
//  StartAccept();
//}

//void Transport::DoSend(const std::string &data,
//                       const Endpoint &endpoint,
//                       const Timeout &timeout) {
//  ip::udp::endpoint ep(endpoint.ip, endpoint.port);
//  bool multiplexer_opened_now(false);
//
//  if (!multiplexer_->IsOpen()) {
//    ReturnCode condition = multiplexer_->Open(ep.protocol());
//    if (kSuccess != condition) {
//      (*on_error_)(condition, endpoint);
//      return;
//    }
//    multiplexer_opened_now = true;
//    // StartDispatch();
//  }
//
//  ConnectionPtr connection(std::make_shared<Connection>(shared_from_this(),
//                                                        strand_,
//                                                        multiplexer_, ep));
//
//  DoInsertConnection(connection);
//  connection->StartSending(data, timeout);
//// Moving StartDispatch() after StartSending(), as on Windows - client-socket's
//// attempt to call async_receive_from() will result in EINVAL error until it is
//// either bound to any port or a sendto() operation is performed by the socket.
//// Also, this makes it in sync with tcp transport's implementation.
//
//  if (multiplexer_opened_now)
//    StartDispatch();
//}

void Transport::InsertConnection(ConnectionPtr connection) {
  strand_.dispatch(std::bind(&Transport::DoInsertConnection,
                             shared_from_this(), connection));
}

void Transport::DoInsertConnection(ConnectionPtr connection) {
  connections_.insert(connection);
  if (std::shared_ptr<ManagedConnections> managed_connections =
      managed_connections_.lock()) {
    managed_connections->InsertEndpoint(
        Endpoint(connection->Socket().RemoteEndpoint()),
        shared_from_this());
  }
}

void Transport::RemoveConnection(ConnectionPtr connection) {
  strand_.dispatch(std::bind(&Transport::DoRemoveConnection,
                             shared_from_this(), connection));
}

void Transport::DoRemoveConnection(ConnectionPtr connection) {
  connections_.erase(connection);
  if (std::shared_ptr<ManagedConnections> managed_connections =
      managed_connections_.lock()) {
    managed_connections->RemoveEndpoint(connection->Socket().RemoteEndpoint());
    if (connections_.empty()) {
      managed_connections->RemoveTransport(shared_from_this());
    }
  }
}

}  // namespace rudp

}  // namespace maidsafe