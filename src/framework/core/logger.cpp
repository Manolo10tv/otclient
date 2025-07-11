/*
 * Copyright (c) 2010-2020 OTClient <https://github.com/edubart/otclient>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "logger.h"
#include "eventdispatcher.h"

//#include <boost/regex.hpp>
#include <framework/core/resourcemanager.h>
#include <framework/core/asyncdispatcher.h>

#ifdef FW_GRAPHICS
#include <framework/platform/platformwindow.h>
#include <framework/platform/platform.h>
#include <framework/luaengine/luainterface.h>
#endif

#ifdef ANDROID
#include <android/log.h>
#endif // ANDROID

Logger g_logger;

namespace
{
    const std::string s_logPrefixes[] = { "", "", "WARNING: ", "ERROR: ", "FATAL ERROR: " };
    bool s_ignoreLogs = false;
}

void Logger::log(Fw::LogLevel level, const std::string& message)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

#ifdef NDEBUG
    if(level == Fw::LogDebug)
        return;
#endif

    if(s_ignoreLogs)
        return;

    std::string outmsg = s_logPrefixes[level] + message;

    /*
#if !defined(NDEBUG) && !defined(WIN32)
    // replace paths for improved debug with vim
    std::stringstream tmp;
    boost::smatch m;
    boost::regex e ("/[^ :]+");
    while(boost::regex_search(outmsg,m,e)) {
        tmp << m.prefix().str();
        tmp << g_resources.getRealDir(m.str()) << m.str();
        outmsg = m.suffix().str();
    }
    if(!tmp.str().empty())
        outmsg = tmp.str();
#endif
    */
#ifdef ANDROID
    __android_log_print(ANDROID_LOG_INFO, "OTClientMobile", outmsg.c_str());
#endif // ANDROID

    std::cout << outmsg << std::endl;

    if(m_outFile.good()) {
        m_outFile << outmsg << std::endl;
        m_outFile.flush();
    }

    std::size_t now = std::time(nullptr);
    m_logMessages.emplace_back(level, outmsg, now);
    if(m_logMessages.size() > MAX_LOG_HISTORY)
        m_logMessages.pop_front();

    if(m_onLog) {
        // schedule log callback, because this callback can run lua code that may affect the current state
        g_dispatcher.addEvent([=] {
            if(m_onLog)
                m_onLog(level, outmsg, now);
        });
    }

    if(level == Fw::LogFatal) {
#ifdef FW_GRAPHICS
        g_window.displayFatalError(message);
#endif
        s_ignoreLogs = true;

        // NOTE: Threads must finish before the process can exit.
        g_asyncDispatcher.terminate();

        exit(-1);
    }
}

void Logger::logFunc(Fw::LogLevel level, const std::string& message, std::string prettyFunction)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    prettyFunction = prettyFunction.substr(0, prettyFunction.find_first_of('('));
    if(prettyFunction.find_last_of(' ') != std::string::npos)
        prettyFunction = prettyFunction.substr(prettyFunction.find_last_of(' ') + 1);


    std::stringstream ss;
    ss << message;

    if(!prettyFunction.empty()) {
        if(g_lua.isInCppCallback())
            ss << g_lua.traceback("", 1);
        ss << g_platform.traceback(prettyFunction, 1, 8);
    }

    log(level, ss.str());
}

void Logger::fireOldMessages()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if(m_onLog) {
        for(const LogMessage& logMessage : m_logMessages) {
            m_onLog(logMessage.level, logMessage.message, logMessage.when);
        }
    }
}

void Logger::setLogFile(const std::string& file)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    m_outFile.open(stdext::utf8_to_latin1(file).c_str(), std::ios::out | std::ios::app);
    if(!m_outFile.is_open() || !m_outFile.good()) {
        g_logger.error(stdext::format("Unable to save log to '%s'", file));
        return;
    }
    m_outFile.flush();
}
