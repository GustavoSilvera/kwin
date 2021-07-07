/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2014 Marco Martin <mart@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CONTRASTSHADER_H
#define CONTRASTSHADER_H

#include <kwinglutils.h>

class QMatrix4x4;

namespace KWin
{

class ContrastShader
{
public:
    ContrastShader();
    virtual ~ContrastShader();

    void init();

    static ContrastShader *create();

    bool isValid() const {
        return mValid;
    }

    void setColorMatrix(const QMatrix4x4 &matrix);
    void setFrostColor(const QColor &color);

    void setTextureMatrix(const QMatrix4x4 &matrix);
    void setModelViewProjectionMatrix(const QMatrix4x4 &matrix);

    void bind();
    void unbind();

    void setOpacity(float opacity);
    float opacity() const;

    void setFrost(bool frost);
    bool frost();

protected:
    void setIsValid(bool value) {
        mValid = value;
    }
    void reset();

private:
    bool mValid;
    GLShader *shader;
    int mvpMatrixLocation;
    int textureMatrixLocation;
    int colorMatrixLocation;
    int frostColorLocation;
    int opacityLocation;
    float m_opacity;
    bool m_frost = false;
};


} // namespace KWin

#endif

