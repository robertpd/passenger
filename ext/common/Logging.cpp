/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2013 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
 *
 *  See LICENSE file for license information.
 */
#include <iomanip>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <Logging.h>
#include <Utils/StrIntUtils.h>

namespace Passenger {

int _logLevel = 0;
int _logOutput = STDERR_FILENO;

int
getLogLevel() {
	return _logLevel;
}

void
setLogLevel(int value) {
	_logLevel = value;
}

bool
setDebugFile(const char *logFile) {
	int fd = open(logFile, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd != -1) {
		_logOutput = fd;
		return true;
	} else {
		return false;
	}
}

void
_prepareLogEntry(std::stringstream &sstream, const char *file, unsigned int line) {
	time_t the_time;
	struct tm the_tm;
	char datetime_buf[60];
	struct timeval tv;

	if (startsWith(file, "ext/")) {
		file += sizeof("ext/") - 1;
		if (startsWith(file, "common/")) {
			file += sizeof("common/") - 1;
			if (startsWith(file, "ApplicationPool2/")) {
				file += sizeof("Application") - 1;
			}
		}
	}
	
	the_time = time(NULL);
	localtime_r(&the_time, &the_tm);
	strftime(datetime_buf, sizeof(datetime_buf) - 1, "%F %H:%M:%S", &the_tm);
	gettimeofday(&tv, NULL);
	sstream <<
		"[ " << datetime_buf << "." << std::setfill('0') << std::setw(4) <<
			(unsigned long) (tv.tv_usec / 100) <<
		" " << std::dec << getpid() << "/" <<
			std::hex << pthread_self() << std::dec <<
		" " << file << ":" << line <<
		" ]: ";
}

void
_writeLogEntry(const std::string &str) {
	size_t written = 0;
	do {
		ssize_t ret = write(_logOutput, str.data() + written, str.size() - written);
		if (ret != -1) {
			written += ret;
		}
	} while (written < str.size());
}

} // namespace Passenger

