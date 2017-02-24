/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2017 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */
#ifndef _PASSENGER_WEB_SOCKET_COMMAND_REVERSE_SERVER_H_
#define _PASSENGER_WEB_SOCKET_COMMAND_REVERSE_SERVER_H_

#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

#include <boost/make_shared.hpp>
#include <boost/function.hpp>
#include <oxt/backtrace.hpp>

#include <string>
#include <vector>

#include <jsoncpp/json.h>

#include <Logging.h>
#include <ConfigStore.h>
#include <Utils/JsonUtils.h>

namespace Passenger {

using namespace std;


/**
 * A generic WebSocket command "server" that implements a request/response
 * model.
 *
 * The reason why the name contains the word "reverse" is because it doesn't
 * actually listens on a port. Instead, it connects to a port and receives
 * commands from there.
 *
 * This class is generic in the sense that it handles all sorts of connection
 * management logic such as reconnecting on failure, handling pings, timeouts,
 * configuration, basic flow control, etc. It doesn't contain any logic for
 * actually handling incoming commands: you are supposed to supply a function
 * for handling incoming commands (the message handler). This allows seperating
 * all the connection management logic from the actual message handling
 * business logic.
 *
 * ## Usage
 *
 *     static bool
 *     onMessage(WebSocketCommandReverseServer *server,
 *         const WebSocketCommandReverseServer::ConnectionWeakPtr &wconn,
 *         const WebSocketCommandReverseServer::MessagePtr &msg)
 *     {
 *         P_INFO("Message received: " << msg->get_payload());
 *         return true;
 *     }
 *
 *
 *     // Set configuration
 *     Json::Value config;
 *     config["url"] = "ws://127.0.0.1:8001/";
 *
 *     // Create and initialize the server
 *     WebSocketCommandReverseServer server(onMessage, config);
 *     server.initialize();
 *
 *     // Enter the server's main loop. This blocks until something
 *     // calls `server.shutdown()`.
 *     server.run();
 *
 * ## About the concurrency and I/O model
 *
 * WebSocketCommandReverseServer uses the WebSocket++ library and the
 * Boost Asio I/O library. WebSocketCommandReverseServer manages its own
 * event loop.
 *
 * The message handler will be called from the event loop's thread, so
 * be careful.
 *
 * ## About flow control and backpressure
 *
 * We purposefully do not implement any flow control/backpressure on the
 * WebSocket's writing side. That is, if we send a large amount of data to
 * the remote, then we do not wait until all that data has actually been
 * sent out before proceeding to read the next message. Unfortunately the
 * WebSocket++ API does not allow us to efficiently implement that.
 * Fortunately, the server knows this and is responsible for not sending
 * another request until it has read the previous request, so in practice
 * we do not run into any problem.
 */
class WebSocketCommandReverseServer {
public:
	typedef websocketpp::client<websocketpp::config::asio_client> Endpoint;
	typedef Endpoint::connection_ptr ConnectionPtr;
	typedef Endpoint::message_ptr MessagePtr;
	typedef websocketpp::connection_hdl ConnectionWeakPtr;

	typedef boost::function<void ()> Callback;
	typedef boost::function<void (const Json::Value &config, const vector<ConfigStore::Error> &errors)> ConfigCallback;
	typedef boost::function<void (const Json::Value &state)> StateCallback;
	typedef boost::function<bool (WebSocketCommandReverseServer *server, const ConnectionWeakPtr &wconn, const MessagePtr &msg)> MessageHandler;

	enum State {
		UNINITIALIZED,
		NOT_CONNECTED,
		CONNECTING,
		WAITING_FOR_REQUEST,
		REPLYING,
		CLOSING,
		SHUT_DOWN
	};

private:
	ConfigStore config;
	string logPrefix;
	Endpoint endpoint;
	ConnectionPtr conn;
	boost::shared_ptr<boost::asio::deadline_timer> timer;
	MessageHandler messageHandler;
	Callback shutdownCallback;
	mutable boost::mutex stateSyncher;
	State state;
	bool reconnectAfterReply;
	bool shuttingDown;

	/**
	 * It could happen that a certain method or handler is invoked
	 * for a connection that has already been closed. For example,
	 * after the message handler was invoked and before the message
	 * handler called doneReplying(), it could happen that the connection
	 * was reset. This method allows detecting those cases so that
	 * the code can decide not to do anything.
	 */
	bool isCurrentConnection(const ConnectionWeakPtr &wconn) {
		return conn && endpoint.get_con_from_hdl(wconn).get() == conn.get();
	}

	void initializeConfigStore() {
		config.registerKey("url",
			ConfigStore::STRING_TYPE,
			ConfigStore::REQUIRED);
		config.registerKey("log_prefix",
			ConfigStore::STRING_TYPE,
			ConfigStore::OPTIONAL);
		config.registerKey("proxy_url",
			ConfigStore::STRING_TYPE,
			ConfigStore::OPTIONAL);
		config.registerKey("proxy_username",
			ConfigStore::STRING_TYPE,
			ConfigStore::OPTIONAL);
		config.registerKey("proxy_password",
			ConfigStore::STRING_TYPE,
			ConfigStore::OPTIONAL);
		config.registerKey("proxy_timeout",
			ConfigStore::FLOAT_TYPE,
			ConfigStore::OPTIONAL,
			ConfigStore::staticDefaultValue(30.0));
		config.registerKey("connect_timeout",
			ConfigStore::FLOAT_TYPE,
			ConfigStore::OPTIONAL,
			ConfigStore::staticDefaultValue(30.0));
		config.registerKey("ping_interval",
			ConfigStore::FLOAT_TYPE,
			ConfigStore::OPTIONAL,
			ConfigStore::staticDefaultValue(30.0));
		config.registerKey("ping_timeout",
			ConfigStore::FLOAT_TYPE,
			ConfigStore::OPTIONAL,
			ConfigStore::staticDefaultValue(30.0));
		config.registerKey("close_timeout",
			ConfigStore::FLOAT_TYPE,
			ConfigStore::OPTIONAL,
			ConfigStore::staticDefaultValue(50.0));
		config.registerKey("reconnect_timeout",
			ConfigStore::FLOAT_TYPE,
			ConfigStore::OPTIONAL,
			ConfigStore::staticDefaultValue(5.0));
	}

	void internalConfigure(const Json::Value &updates, const ConfigCallback callback) {
		vector<ConfigStore::Error> errors;
		Json::Value preview = config.previewUpdate(updates, errors);
		if (!errors.empty()) {
			if (callback) {
				callback(preview, errors);
			}
			return;
		}

		ConfigStore newConfig(config);
		newConfig.forceApplyUpdatePreview(preview);
		bool shouldReconnect =
			newConfig["url"].asString() != config["url"].asString() ||
			newConfig["proxy_url"].asString() != config["proxy_url"].asString();
		config.forceApplyUpdatePreview(preview);
		updateConfigCache();

		if (shouldReconnect) {
			internalReconnect();
		}

		if (callback) {
			callback(preview, errors);
		}
	}

	void updateConfigCache() {
		logPrefix = config["log_prefix"].asString();
	}

	void internalInspectConfig(const ConfigCallback callback) {
		callback(config.dump(), vector<ConfigStore::Error>());
	}

	void internalInspectState(const StateCallback callback) {
		Json::Value doc(Json::objectValue);
		doc["state"] = getStateString();
		if (reconnectAfterReply) {
			doc["reconnect_planned"] = true;
		}
		if (shuttingDown) {
			doc["shutting_down"] = true;
		}
		callback(doc);
	}

	string getStateString() const {
		boost::lock_guard<boost::mutex> l(stateSyncher);
		switch (state) {
		case UNINITIALIZED:
			return "UNINITIALIZED";
		case NOT_CONNECTED:
			return "NOT_CONNECTED";
		case CONNECTING:
			return "CONNECTING";
		case WAITING_FOR_REQUEST:
			return "WAITING_FOR_REQUEST";
		case REPLYING:
			return "REPLYING";
		case CLOSING:
			return "CLOSING";
		case SHUT_DOWN:
			return "SHUT_DOWN";
		default:
			return "UNKNOWN";
		};
	}

	void internalShutdown(const Callback callback) {
		shuttingDown = true;
		shutdownCallback = callback;
		closeConnection(websocketpp::close::status::going_away,
			"shutting down");
	}

	void startConnect() {
		websocketpp::lib::error_code ec;

		{
			boost::lock_guard<boost::mutex> l(stateSyncher);
			state = CONNECTING;
		}

		P_NOTICE(logPrefix << "Connecting to " << config["url"].asString());
		conn = endpoint.get_connection(config["url"].asString(), ec);
		if (ec) {
			P_ERROR(logPrefix << "Error setting up a socket to "
				<< config["url"].asString() << ": " << ec.message());
			{
				boost::lock_guard<boost::mutex> l(stateSyncher);
				state = NOT_CONNECTED;
			}
			scheduleReconnect();
			return;
		}

		if (!applyConnectionConfig(conn)) {
			// applyConnectionConfig() already logs an error.
			{
				boost::lock_guard<boost::mutex> l(stateSyncher);
				state = NOT_CONNECTED;
			}
			scheduleReconnect();
			return;
		}

		using websocketpp::lib::placeholders::_1;
		using websocketpp::lib::placeholders::_2;

		conn->set_socket_init_handler(websocketpp::lib::bind(
			&WebSocketCommandReverseServer::onSocketInit,
			this,
			_1,
			_2));
		conn->set_open_handler(websocketpp::lib::bind(
			&WebSocketCommandReverseServer::onConnected,
			this,
			_1));
		conn->set_fail_handler(websocketpp::lib::bind(
			&WebSocketCommandReverseServer::onConnectFailed,
			this,
			websocketpp::lib::placeholders::_1));
		conn->set_close_handler(websocketpp::lib::bind(
			&WebSocketCommandReverseServer::onConnectionClosed,
			this,
			websocketpp::lib::placeholders::_1));
		conn->set_pong_timeout_handler(websocketpp::lib::bind(
			&WebSocketCommandReverseServer::onPongTimeout,
			this,
			_1,
			_2));
		conn->set_pong_handler(websocketpp::lib::bind(
			&WebSocketCommandReverseServer::onPong,
			this,
			_1,
			_2));
		conn->set_message_handler(websocketpp::lib::bind(
			&WebSocketCommandReverseServer::onMessage,
			this,
			_1,
			_2));

		endpoint.connect(conn);
	}

	bool applyConnectionConfig(ConnectionPtr &conn) {
		websocketpp::lib::error_code ec;

		if (!config["proxy_url"].isNull()) {
			conn->set_proxy(config["proxy_url"].asString(), ec);
			if (ec) {
				P_ERROR(logPrefix << "Error setting proxy URL to "
					<< config["proxy_url"].asString() << ": "
					<< ec.message());
				return false;
			}

			if (!config["proxy_username"].isNull() || !config["proxy_password"].isNull()) {
				conn->set_proxy_basic_auth(config["proxy_username"].asString(),
					config["proxy_password"].asString(), ec);
				if (ec) {
					P_ERROR(logPrefix << "Error setting proxy authentication credentials to "
						<< config["proxy_username"].asString() << ":<password omitted>:"
						<< ec.message());
					return false;
				}
			}

			conn->set_proxy_timeout(config["proxy_timeout"].asDouble() * 1000, ec);
			if (ec) {
				P_ERROR(logPrefix << "Error setting proxy timeout to "
					<< config["proxy_timeout"].asDouble() << " seconds: "
					<< ec.message());
				return false;
			}
		}

		conn->set_open_handshake_timeout(config["connect_timeout"].asDouble() * 1000);
		conn->set_pong_timeout(config["ping_timeout"].asDouble() * 1000);
		conn->set_close_handshake_timeout(config["close_timeout"].asDouble() * 1000);

		return true;
	}

	void internalReconnect() {
		switch (state) {
		case NOT_CONNECTED:
			// Do nothing.
			break;
		case CONNECTING:
		case WAITING_FOR_REQUEST:
			closeConnection(websocketpp::close::status::service_restart,
				"reestablishing connection in order to apply configuration updates");
			break;
		case REPLYING:
			reconnectAfterReply = true;
			return;
		default:
			P_BUG("Unsupported state " + toString(state));
		}
	}

	void scheduleReconnect() {
		P_NOTICE(logPrefix << "Reestablishing connection in " <<
			config["reconnect_timeout"].asDouble() << " seconds");
		restartTimer(config["reconnect_timeout"].asDouble() * 1000);
	}

	void closeConnection(websocketpp::close::status::value code,
		const string &reason)
	{
		websocketpp::lib::error_code ec;

		{
			boost::lock_guard<boost::mutex> l(stateSyncher);
			state = CLOSING;
		}

		P_NOTICE(logPrefix << "Closing connection: " << reason);
		reconnectAfterReply = false;
		timer->cancel();
		conn->close(code, reason, ec);

		if (ec) {
			P_WARN(logPrefix << "Error closing connection: " << ec.message());
			{
				boost::lock_guard<boost::mutex> l(stateSyncher);
				state = NOT_CONNECTED;
			}
			scheduleReconnect();
		}
	}

	void restartTimer(unsigned int ms) {
		timer->expires_from_now(boost::posix_time::milliseconds(ms));
		timer->async_wait(boost::bind(
			&WebSocketCommandReverseServer::onTimeout,
			this,
			boost::placeholders::_1));
	}

	void onSocketInit(ConnectionWeakPtr wconn, boost::asio::ip::tcp::socket &s) {
		boost::asio::ip::tcp::no_delay option(true);
		s.set_option(option);
	}

	void onConnected(ConnectionWeakPtr wconn) {
		if (!isCurrentConnection(wconn)) {
			P_DEBUG(logPrefix << "onConnected: not current connection");
			return;
		}

		P_NOTICE(logPrefix << "Connection established");
		{
			boost::lock_guard<boost::mutex> l(stateSyncher);
			state = WAITING_FOR_REQUEST;
		}
		P_DEBUG(logPrefix << "Scheduling next ping in " <<
			config["ping_interval"].asDouble() << " seconds");
		restartTimer(config["ping_interval"].asDouble() * 1000);
	}

	void onConnectFailed(ConnectionWeakPtr wconn) {
		if (!isCurrentConnection(wconn)) {
			P_DEBUG(logPrefix << "onConnectFailed: not current connection");
			return;
		}

		P_NOTICE(logPrefix << "Unable to establish connection: " <<
			conn->get_ec().message());
		{
			boost::lock_guard<boost::mutex> l(stateSyncher);
			state = NOT_CONNECTED;
		}
		scheduleReconnect();
	}

	void onConnectionClosed(ConnectionWeakPtr wconn) {
		if (!isCurrentConnection(wconn)) {
			P_DEBUG(logPrefix << "onConnectionClosed: not current connection");
			return;
		}

		if (getLogLevel() >= LVL_NOTICE) {
			P_NOTICE(logPrefix << "Connection closed (server close reason: " <<
				conn->get_remote_close_code() << ": " <<
				conn->get_remote_close_reason() << ")");
		}
		{
			boost::lock_guard<boost::mutex> l(stateSyncher);
			state = NOT_CONNECTED;
		}
		reconnectAfterReply = false;

		if (shuttingDown) {
			timer->cancel();
		} else {
			scheduleReconnect();
		}
	}

	void onTimeout(const boost::system::error_code &e) {
		if (e.value() == boost::system::errc::operation_canceled) {
			P_DEBUG(logPrefix << "onTimeout: operation cancelled");
			return;
		}
		if (e) {
			P_ERROR(logPrefix << "Error in timer: " << e.message());
			return;
		}

		websocketpp::lib::error_code ec;

		switch (state) {
		case NOT_CONNECTED:
			startConnect();
			break;
		case WAITING_FOR_REQUEST:
		case REPLYING:
			P_DEBUG(logPrefix << "Sending ping");
			conn->ping("ping", ec);
			if (ec) {
				closeConnection(websocketpp::close::status::normal,
					"error sending ping");
			}
			// After sending the ping, we wait until either
			// onPong() or onPongTimeout() is called before
			// scheduling the next ping.
			break;
		default:
			P_BUG("Unsupported state " + toString(state));
			break;
		}
	}

	void onPongTimeout(ConnectionWeakPtr wconn, const string &payload) {
		if (!isCurrentConnection(wconn)) {
			P_DEBUG(logPrefix << "onPongTimeout: not current connection");
			return;
		}

		switch (state) {
		case REPLYING:
			// Ignore pong timeouts while replying because
			// reading is paused while replying.
			P_DEBUG(logPrefix << "onPongTimeout: ignoring REPLYING state");
			break;
		default:
			P_DEBUG(logPrefix << "onPongTimeout: closing connection");
			closeConnection(websocketpp::close::status::normal,
				"reconnecting because of pong timeout");
			break;
		}
	}

	void onPong(ConnectionWeakPtr wconn, const string &payload) {
		if (!isCurrentConnection(wconn)) {
			P_DEBUG(logPrefix << "onPong: not current connection");
			return;
		}

		P_DEBUG(logPrefix << "Pong received. Scheduling next ping in " <<
			config["ping_interval"].asDouble() << " seconds");
		restartTimer(config["ping_interval"].asDouble() * 1000);
	}

	void onMessage(ConnectionWeakPtr wconn, MessagePtr msg) {
		if (!isCurrentConnection(wconn)) {
			P_DEBUG(logPrefix << "onMessage: not current connection");
			return;
		}

		switch (state) {
		case WAITING_FOR_REQUEST:
			P_DEBUG(logPrefix << "onMessage: got frame of " << msg->get_payload().size() << " bytes");
			{
				boost::lock_guard<boost::mutex> l(stateSyncher);
				state = REPLYING;
			}
			if (messageHandler(this, wconn, msg)) {
				doneReplying(wconn);
			} else {
				conn->pause_reading();
			}
			break;
		case CLOSING:
			// Ignore any incoming messages while closing.
			P_DEBUG(logPrefix << "onMessage: ignoring CLOSING state");
			break;
		default:
			P_BUG("Unsupported state " + toString(state));
		}
	}

public:
	WebSocketCommandReverseServer(const MessageHandler &_messageHandler, const Json::Value &config)
		: messageHandler(_messageHandler),
		  state(UNINITIALIZED),
		  reconnectAfterReply(false),
		  shuttingDown(false)
	{
		vector<ConfigStore::Error> errors;

		initializeConfigStore();
		if (!this->config.update(config, errors)) {
			throw ArgumentException("Invalid configuration: " + toString(errors));
		}
		updateConfigCache();
	}

	void initialize() {
		endpoint.clear_access_channels(websocketpp::log::alevel::all);
        endpoint.clear_error_channels(websocketpp::log::elevel::all);
		endpoint.init_asio();
		state = NOT_CONNECTED;
		timer = boost::make_shared<boost::asio::deadline_timer>(
			endpoint.get_io_service());
		startConnect();
	}

	/**
	 * Enter the server's event loop. This method blocks until
	 * the server is shut down.
	 *
	 * May only be called once, and only after `initialize()` is called.
	 */
	void run() {
		endpoint.run();
		{
			boost::lock_guard<boost::mutex> l(stateSyncher);
			state = SHUT_DOWN;
		}
		if (shutdownCallback) {
			shutdownCallback();
		}
	}


	/**
	 * Change the server's configuration.
	 *
	 * The configuration change will be applied in the next event loop
	 * tick, not immediately. When the change is applied, the given
	 * callback (if any) will be called from the event loop thread.
	 *
	 * May only be called when the event loop is running.
	 * This method is thread-safe and may be called from any thread.
	 */
	void configure(const Json::Value &doc,
		const ConfigCallback &callback = ConfigCallback())
	{
		endpoint.get_io_service().post(boost::bind(
			&WebSocketCommandReverseServer::internalConfigure,
			this, doc, callback));
	}

	/**
	 * Inspect the server's configuration. Fetching the configuration
	 * will be performed in the next event loop tick, not immediately.
	 * When done, the given callback will be called, from the event loop
	 * thread, with the inspection result.
	 *
	 * May only be called when the event loop is running.
	 * This method is thread-safe and may be called from any thread.
	 */
	void inspectConfig(const ConfigCallback &callback) {
		endpoint.get_io_service().post(boost::bind(
			&WebSocketCommandReverseServer::internalInspectConfig,
			this, callback));
	}

	/**
	 * Inspect the server's state. Fetching the state
	 * will be performed in the next event loop tick, not immediately.
	 * When done, the given callback will be called, from the event loop
	 * thread, with the inspection result.
	 *
	 * May only be called when the event loop is running.
	 * This method is thread-safe and may be called from any thread.
	 */
	void inspectState(const StateCallback &callback) {
		endpoint.get_io_service().post(boost::bind(
			&WebSocketCommandReverseServer::internalInspectState,
			this, callback));
	}

	/**
	 * When the message handler is done sending a reply, it must
	 * call this method to tell the server that the reply is done.
	 *
	 * May only be called when the server is in the REPLYING state.
	 * May only be called from the event loop's thread.
	 */
	void doneReplying(const ConnectionWeakPtr &wconn) {
		if (!isCurrentConnection(wconn)) {
			P_DEBUG(logPrefix << "doneReplying: not current connection");
			return;
		}

		P_DEBUG(logPrefix << "done replying");
		P_ASSERT_EQ(state, REPLYING);
		{
			boost::lock_guard<boost::mutex> l(stateSyncher);
			state = WAITING_FOR_REQUEST;
		}
		conn->resume_reading();
		if (reconnectAfterReply) {
			reconnectAfterReply = false;
			internalReconnect();
		}
	}

	/**
	 * Prepares this server for shut down. It will finish any replies that
	 * are in-flight and will close the connection. When finished, it will
	 * call the given callback (if any) from the thread that invoked
	 * `run()`.
	 *
	 * May only be called when the event loop is running.
	 * This method is thread-safe and may be called from any thread.
	 */
	void shutdown(const Callback &callback = Callback()) {
		endpoint.get_io_service().post(boost::bind(
			&WebSocketCommandReverseServer::internalShutdown,
			this, callback));
	}
};


} // namespace Passenger

#endif /* _PASSENGER_WEB_SOCKET_COMMAND_REVERSE_SERVER_H_ */
