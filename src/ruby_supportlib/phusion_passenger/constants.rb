#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2015 Phusion Holding B.V.
#
#  "Passenger", "Phusion Passenger" and "Union Station" are registered
#  trademarks of Phusion Holding B.V.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to deal
#  in the Software without restriction, including without limitation the rights
#  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#  copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in
#  all copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
#  THE SOFTWARE.

module PhusionPassenger
  PASSENGER_TXN_ID            = "PASSENGER_TXN_ID".freeze
  PASSENGER_APP_GROUP_NAME    = "PASSENGER_APP_GROUP_NAME".freeze
  PASSENGER_DELTA_MONOTONIC   = "PASSENGER_DELTA_MONOTONIC".freeze
  RACK_HIJACK_IO              = "rack.hijack_io".freeze

  LVL_CRIT   = 0
  LVL_ERROR  = 1
  LVL_WARN   = 2
  LVL_NOTICE = 3
  LVL_INFO   = 4
  LVL_DEBUG  = 5
  LVL_DEBUG2 = 6
  LVL_DEBUG3 = 7

  # Constants shared between the C++ and Ruby codebase. The C++ Constants.h
  # is automatically generated by the build system from the following
  # definitions.
  module SharedConstants
    # Default config values
    DEFAULT_LOG_LEVEL = 3
    DEFAULT_INTEGRATION_MODE = "standalone"
    DEFAULT_SOCKET_BACKLOG = 2048
    DEFAULT_RUBY = "ruby"
    DEFAULT_PYTHON = "python"
    DEFAULT_NODEJS = "node"
    DEFAULT_MAX_POOL_SIZE = 6
    DEFAULT_POOL_IDLE_TIME = 300
    DEFAULT_MAX_PRELOADER_IDLE_TIME = 5 * 60
    DEFAULT_START_TIMEOUT = 90_000
    DEFAULT_WEB_APP_USER = "nobody"
    DEFAULT_APP_ENV = "production"
    DEFAULT_SPAWN_METHOD = "smart"
    # Apache's unixd.h also defines DEFAULT_USER, so we avoid naming clash here.
    PASSENGER_DEFAULT_USER = "nobody"
    DEFAULT_CONCURRENCY_MODEL = "process"
    DEFAULT_STICKY_SESSIONS_COOKIE_NAME = "_passenger_route"
    DEFAULT_APP_THREAD_COUNT = 1
    DEFAULT_RESPONSE_BUFFER_HIGH_WATERMARK = 1024 * 1024 * 128
    DEFAULT_MAX_REQUEST_QUEUE_SIZE = 100
    DEFAULT_MAX_REQUEST_QUEUE_TIME = 0
    DEFAULT_STAT_THROTTLE_RATE = 10
    DEFAULT_ANALYTICS_LOG_USER = DEFAULT_WEB_APP_USER
    DEFAULT_ANALYTICS_LOG_GROUP = ""
    DEFAULT_ANALYTICS_LOG_PERMISSIONS = "u=rwx,g=rx,o=rx"
    DEFAULT_UNION_STATION_GATEWAY_ADDRESS = "gateway.unionstationapp.com"
    DEFAULT_UNION_STATION_GATEWAY_PORT = 443
    DEFAULT_HTTP_SERVER_LISTEN_ADDRESS = "tcp://127.0.0.1:3000"
    DEFAULT_UST_ROUTER_LISTEN_ADDRESS = "tcp://127.0.0.1:9344"
    DEFAULT_LVE_MIN_UID = 500

    # Size limits
    MESSAGE_SERVER_MAX_USERNAME_SIZE = 100
    MESSAGE_SERVER_MAX_PASSWORD_SIZE = 100
    POOL_HELPER_THREAD_STACK_SIZE = 1024 * 256
    # Small mbuf sizes avoid memory overhead (up to 1 blocksize per request), but
    # also introduce context switching and smaller transfer writes. The size is picked 
    # to balance this out.
    DEFAULT_MBUF_CHUNK_SIZE = 1024 * 4
    # Affects input and output buffering (between app and client). Threshold is picked
    # such that it fits most output (i.e. html page size, not assets), and allows for
    # high concurrency with low mem overhead. On the upload side there is a penalty 
    # but there's no real average upload size anyway so we choose mem safety instead. 
    DEFAULT_FILE_BUFFERED_CHANNEL_THRESHOLD = 1024 * 128
    SERVER_KIT_MAX_SERVER_ENDPOINTS = 4

    # Time limits
    PROCESS_SHUTDOWN_TIMEOUT = 60 # In seconds
    PROCESS_SHUTDOWN_TIMEOUT_DISPLAY = "1 minute"

    # Versions
    PASSENGER_VERSION = PhusionPassenger::VERSION_STRING
    PASSENGER_API_VERSION_MAJOR = 0
    PASSENGER_API_VERSION_MINOR = 3
    PASSENGER_API_VERSION = "#{PASSENGER_API_VERSION_MAJOR}.#{PASSENGER_API_VERSION_MINOR}"
    SERVER_INSTANCE_DIR_STRUCTURE_MAJOR_VERSION = 4
    SERVER_INSTANCE_DIR_STRUCTURE_MINOR_VERSION = 0
    SERVER_INSTANCE_DIR_STRUCTURE_MIN_SUPPORTED_MINOR_VERSION = 0

    # Misc
    FEEDBACK_FD = 3
    PROGRAM_NAME = "Phusion Passenger"
    SHORT_PROGRAM_NAME = "Passenger"
    SERVER_TOKEN_NAME = "Phusion_Passenger"
    FLYING_PASSENGER_NAME = "Flying Passenger"
    SUPPORT_URL         = "https://www.phusionpassenger.com/support"
    ENTERPRISE_URL      = "https://www.phusionpassenger.com/enterprise"
    GLOBAL_NAMESPACE_DIRNAME            = PhusionPassenger::GLOBAL_NAMESPACE_DIRNAME_
    # Subdirectory under $HOME to use for storing stuff.
    USER_NAMESPACE_DIRNAME              = PhusionPassenger::USER_NAMESPACE_DIRNAME_
    AGENT_EXE                 = "PassengerAgent"
    DEB_MAIN_PACKAGE          = "passenger"
    DEB_DEV_PACKAGE           = "passenger-dev"
    DEB_APACHE_MODULE_PACKAGE = "libapache2-mod-passenger"
    DEB_NGINX_PACKAGE         = "nginx-extras"
    RPM_MAIN_PACKAGE          = "passenger"
    RPM_DEV_PACKAGE           = "passenger-devel"
    RPM_APACHE_MODULE_PACKAGE = "mod_passenger"
    RPM_NGINX_PACKAGE         = "nginx"
  end

  SharedConstants.constants.each do |name|
    const_set(name, SharedConstants.const_get(name)) unless const_defined? name
  end
end
