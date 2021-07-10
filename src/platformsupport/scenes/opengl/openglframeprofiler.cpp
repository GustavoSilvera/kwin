/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "openglframeprofiler.h"
#include "logging.h"

#include "kwinglplatform.h"
#include "kwinglutils.h"

namespace KWin
{

OpenGLFrameProfiler::OpenGLFrameProfiler()
{
    if (GLPlatform::instance()->supports(GLFeature::TimerQuery)) {
        glGenQueries(m_queries.size(), m_queries.data());
    }
}

OpenGLFrameProfiler::~OpenGLFrameProfiler()
{
    if (m_queries[0] || m_queries[1]) {
        glDeleteQueries(m_queries.size(), m_queries.data());
    }
}

void OpenGLFrameProfiler::begin()
{
    if (m_queries[0]) {
        glGetInteger64v(GL_TIMESTAMP, reinterpret_cast<GLint64 *>(&m_cpuStart));
        glQueryCounter(m_queries[0], GL_TIMESTAMP);
    } else {
        m_cpuStart = std::chrono::steady_clock::now().time_since_epoch().count();
    }
}

void OpenGLFrameProfiler::end()
{
    if (m_queries[1]) {
        glGetInteger64v(GL_TIMESTAMP, reinterpret_cast<GLint64 *>(&m_cpuEnd));
        glQueryCounter(m_queries[1], GL_TIMESTAMP);
    } else {
        m_cpuEnd = std::chrono::steady_clock::now().time_since_epoch().count();
    }
}

std::chrono::nanoseconds OpenGLFrameProfiler::result()
{
    std::chrono::nanoseconds start(m_cpuStart);
    std::chrono::nanoseconds end(m_cpuEnd);

    if (m_queries[0] && m_queries[1]) {
        GLuint64 gpuStart = 0;
        glGetQueryObjectui64v(m_queries[0], GL_QUERY_RESULT, &gpuStart);

        GLuint64 gpuEnd = 0;
        glGetQueryObjectui64v(m_queries[1], GL_QUERY_RESULT, &gpuEnd);

        if (gpuStart && gpuEnd && gpuStart <= gpuEnd) {
            start = std::min(start, std::chrono::nanoseconds(gpuStart));
            end = std::max(end, std::chrono::nanoseconds(gpuEnd));
        } else {
            qCDebug(KWIN_OPENGL, "Invalid GPU render timestamps (start: %ld, end: %ld)", gpuStart, gpuEnd);
        }
    }

    return end - start;
}

} // namespace KWin
