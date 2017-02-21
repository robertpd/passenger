#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

#include <boost/make_shared.hpp>
#include <boost/function.hpp>
#include <oxt/backtrace.hpp>

#include <string>
#include <vector>

#include <jsoncpp/json.h>

#include <Logging.h>
#include <JsonSchema.h>
#include <Utils/JsonUtils.h>

namespace Passenger {

using namespace std;

/**
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
	typedef boost::function<void (const ConnectionWeakPtr &wconn, const MessagePtr &msg)> MessageHandler;

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
	boost::mutex stateSyncher;
	State state;
	ConfigStore config;
	string logPrefix;
	Endpoint endpoint;
	ConnectionPtr conn;
	boost::shared_ptr<boost::asio::deadline_timer> timer;
	MessageHandler messageHandler;
	Callback shutdownCallback;
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

	initializeConfigStore() {
		store.registerKey("url",
			ConfigStore::STRING_TYPE,
			ConfigStore::REQUIRED);
		store.registerKey("log_prefix",
			ConfigStore::STRING_TYPE,
			ConfigStore::OPTIONAL);
		store.registerKey("proxy_url",
			ConfigStore::STRING_TYPE,
			ConfigStore::OPTIONAL);
		store.registerKey("proxy_username",
			ConfigStore::STRING_TYPE,
			ConfigStore::OPTIONAL);
		store.registerKey("proxy_password",
			ConfigStore::STRING_TYPE,
			ConfigStore::OPTIONAL);
		store.registerKey("proxy_timeout",
			ConfigStore::UNSIGNED_INTEGER_TYPE,
			ConfigStore::OPTIONAL,
			ConfigStore::defaultValue(30000));
		store.registerKey("connect_timeout",
			ConfigStore::UNSIGNED_INTEGER_TYPE,
			ConfigStore::OPTIONAL,
			ConfigStore::defaultValue(30000));
		store.registerKey("ping_timeout",
			ConfigStore::UNSIGNED_INTEGER_TYPE,
			ConfigStore::OPTIONAL,
			ConfigStore::defaultValue(30000));
		store.registerKey("close_timeout",
			ConfigStore::UNSIGNED_INTEGER_TYPE,
			ConfigStore::OPTIONAL,
			ConfigStore::defaultValue(5000));
	}

#if 0
		// effective value calculation
		// caching

		on update:
			config.update(updates);
			// => sets logPrefix

		normal operation:
			connect(config["url"].asString());
			P_INFO(logPrefix << "");

		on dump:
			config.dump();
			// => prints all actual values and effective values
			// cache not used
#endif

	void internalConfigure(const Json::Value &updates, const ConfigCallback callback) {
		vector<ConfigStore::Error> errors;
		Json::Value preview = config.previewUpdate(updates, errors);
		if (errors.empty()) {
			if (callback) {
				callback(preview, errors);
			}
			return false;
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
			callback(newConfig, errors);
		}
	}

	void updateConfigCache() {
		logPrefix = config["log_prefix"].asString();
	}

	void internalShutdownAndCallCallback(const Callback callback) {
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

		conn = endpoint.get_connection(config["url"].asString(), ec);
		if (ec) {
			P_ERROR(logPrefix << "Error setting up a socket to "
				<< config["url"].asString() << ": " << ec.message());
			boost::lock_guard<boost::mutex> l(stateSyncher);
			state = NOT_CONNECTED;
			return;
		}

		if (!applyConnectionConfig(conn)) {
			// applyConnectionConfig() already logs an error.
			boost::lock_guard<boost::mutex> l(stateSyncher);
			state = NOT_CONNECTED;
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
		conn->set_message_handler(websocketpp::lib::bind(
			&WebSocketCommandReverseServer::onMessage,
			this,
			_1,
			_2));

		endpoint.connect(conn);
	}

	bool applyConnectionConfig() {
		if (!config["proxy_url"].isNull()) {
			conn.set_proxy(config["proxy_url"].asString(), ec);
			if (ec) {
				P_ERROR(logPrefix << "Error setting up proxy URL to "
					<< config["proxy_url"].asString() << ": "
					<< ec.message());
				return false;
			}
		}

		if (!config["proxy_username"].isNull() || !config["proxy_password"].isNull()) {
			conn.set_proxy_basic_auth(config["proxy_username"].asString(),
				config["proxy_password"].asString(), ec);
			if (ec) {
				P_ERROR(logPrefix << "Error setting up proxy authentication credentials to "
					<< config["proxy_username"].asString() << ":<password omitted>:"
					<< ec.message());
				return false;
			}
		}

		if (!config["proxy_timeout"].isNull()) {
			conn.set_proxy_timeout(config["proxy_timeout"].asUInt(), ec);
			if (ec) {
				P_ERROR(logPrefix << "Error setting up proxy URL to "
					<< config["proxy_url"].asString() << ": "
					<< ec.message());
				return false;
			}
		}

		if (!config["connect_timeout"].isNull()) {
			conn.set_open_handshake_timeout(config["connect_timeout"].asUInt());
		}
		if (!config["ping_timeout"].isNull()) {
			conn.set_pong_timeout(config["ping_timeout"].asUInt());
		}
		if (!config["close_timeout"].isNull()) {
			conn.set_close_handshake_timeout(config["close_timeout"].asUInt());
		}

		return true;
	}

	void internalReconnect() {
		switch (state) {
		case NOT_CONNECTED:
			// Do nothing.
			break;
		case CONNECTING:
		case WAITING_FOR_REQUEST:
			{
				boost::lock_guard<boost::mutex> l(stateSyncher);
				state = CLOSING;
			}
			conn->close(websocketpp::close::status::service_restart,
				"reconnecting");
			break;
		case REPLYING:
			reconnectAfterReply = true;
			return;
		default:
			P_BUG("Unsupported state " + toString(state));
		}
	}

	void closeConnection(websocketpp::close::status::value code,
		const string &reason)
	{
		{
			boost::lock_guard<boost::mutex> l(stateSyncher);
			state = CLOSING;
		}
		reconnectAfterReply = false;
		timer->cancel();
		conn->close(websocketpp::close::status::normal,
			"reconnecting because of pong timeout");
	}

	void onSocketInit(ConnectionWeakPtr wconn, boost::asio::ip::tcp::socket &s) {
		boost::asio::ip::tcp::no_delay option(true);
		s.set_option(option);
	}

	void onConnected(ConnectionWeakPtr wconn) {
		P_WARN("onConnected");

		if (!isCurrentConnection(wconn)) {
			return;
		}

		{
			boost::lock_guard<boost::mutex> l(stateSyncher);
			state = WAITING_FOR_REQUEST;
		}

		timer->expires_from_now(boost::posix_time::seconds(30));
		timer->async_wait(boost::bind(
			&WebSocketCommandReverseServer::onTimeout,
			this,
			boost::placeholders::_1));
	}

	void onConnectFailed(ConnectionWeakPtr wconn) {
		P_WARN("onConnectFailed");

		if (!isCurrentConnection(wconn)) {
			return;
		}

		{
			boost::lock_guard<boost::mutex> l(stateSyncher);
			state = NOT_CONNECTED;
		}
		//conn->get_ec().message();

		timer->expires_from_now(boost::posix_time::seconds(5));
		timer->async_wait(boost::bind(
			&WebSocketCommandReverseServer::onTimeout,
			this,
			boost::placeholders::_1));
	}

	void onConnectionClosed(ConnectionWeakPtr wconn) {
		P_WARN("onConnectionClosed");

		if (!isCurrentConnection(wconn)) {
			return;
		}

		{
			boost::lock_guard<boost::mutex> l(stateSyncher);
			state = NOT_CONNECTED;
		}
		reconnectAfterReply = false;

		if (shuttingDown) {
			timer->cancel();
		} else {
			// Schedule reconnect.
			timer->expires_from_now(boost::posix_time::seconds(5));
			timer->async_wait(boost::bind(
				&WebSocketCommandReverseServer::onTimeout,
				this,
				boost::placeholders::_1));
		}
	}

	void onTimeout(const boost::system::error_code &e) {
		P_WARN("onTimeout: " << e.message());

		if (e.value() == boost::system::errc::operation_canceled) {
			return;
		}

		websocketpp::lib::error_code ec;

		switch (state) {
		case NOT_CONNECTED:
			startConnect();
			break;
		case WAITING_FOR_REQUEST:
		case REPLYING:
			P_WARN("pinging");
			conn->ping("ping", ec);
			if (ec) {
				closeConnection(websocketpp::close::status::normal,
					"error sending ping");
			} else {
				timer->expires_from_now(boost::posix_time::seconds(30));
				timer->async_wait(boost::bind(
					&WebSocketCommandReverseServer::onTimeout,
					this,
					boost::placeholders::_1));
			}
			break;
		default:
			P_BUG("Unsupported state " + toString(state));
			break;
		}
	}

	void onPongTimeout(ConnectionWeakPtr wconn, const string &payload) {
		P_WARN("onPongTimeout");

		if (!isCurrentConnection(wconn)) {
			return;
		}

		switch (state) {
		case REPLYING:
			// Ignore pong timeouts while replying because
			// reading is paused while replying.
			break;
		default:
			closeConnection(websocketpp::close::status::normal,
				"reconnecting because of pong timeout");
			break;
		}
	}

	void onMessage(ConnectionWeakPtr wconn, MessagePtr msg) {
		P_WARN("onMessage");

		switch (state) {
		case WAITING_FOR_REQUEST:
			{
				boost::lock_guard<boost::mutex> l(stateSyncher);
				state = REPLYING;
			}
			conn->pause_reading();
			messageHandler(wconn, msg);
			break;
		case CLOSING:
			// Ignore any incoming messages while closing.
			break;
		default:
			P_BUG("Unsupported state " + toString(state));
		}
	}

public:
	WebSocketCommandReverseServer(const Json::Value &config)
		: state(UNINITIALIZED),
		  reconnectAfterReply(false),
		  shuttingDown(false)
	{
		vector<string> errors;

		initializeConfigStore();
		if (!this->config.update(config, errors)) {
			throw ArgumentException("Invalid configuration: " + toString(errors));
		}
		updateConfigCache();
	}

	void initialize() {
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
	 * May only be called once, and only if `isInitialized()`.
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
	 * Returns whether `initialize()` is called. Note that even after
	 * shutdown, `isInitialized()` is still true.
	 *
	 * This method is thread-safe.
	 */
	bool isInitialized() const {
		boost::lock_guard<boost::mutex> l(stateSyncher);
		return state != UNINITIALIZED;
	}

	/**
	 * Returns whether the server is done shutting down.
	 *
	 * This method is thread-safe.
	 */
	bool isShutDown() const {
		boost::lock_guard<boost::mutex> l(stateSyncher);
		return state != SHUT_DOWN;
	}


	/**
	 * Change the server's configuration.
	 *
	 * The configuration change will be applied in the next event loop
	 * tick, not immediately. When the change is applied, the given
	 * callback (if any) will be called from the event loop thread.
	 *
	 * May only be called after the server is initialized, but before it is shut down.
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
	 * When the message handler is done sending a reply, it must
	 * call this method to tell the server that the reply is done.
	 *
	 * May only be called when the server is in the REPLYING state.
	 * May only be called from the event loop's thread.
	 */
	void doneReplying(const ConnectionWeakPtr &wconn) {
		if (!isCurrentConnection(wconn)) {
			return;
		}

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
	 * May only be called if `isInitialized() && !isShutDown()`.
	 * May be called when the event loop is not running, but note that
	 * the callback is only called after the event loop quits.
	 * This method is thread-safe and may be called from any thread.
	 */
	void shutdown(const Callback &callback = Callback()) {
		endpoint.get_io_service().post(boost::bind(
			&WebSocketCommandReverseServer::internalShutdownAndCallCallback,
			this,
			callback));
	}
};


} // namespace Passenger
