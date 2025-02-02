// ****************************************************************************
//  renderer.cc                                                   DB48X project
// ****************************************************************************
//
//   File Description:
//
//     Structure used to render objects
//
//
//
//
//
//
//
//
// ****************************************************************************
//   (C) 2022 Christophe de Dinechin <christophe@dinechin.org>
//   This software is licensed under the terms outlined in LICENSE.txt
// ****************************************************************************
//   This file is part of DB48X.
//
//   DB48X is free software: you can redistribute it and/or modify
//   it under the terms outlined in the LICENSE.txt file
//
//   DB48X is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// ****************************************************************************

#include "renderer.h"

#include "file.h"
#include "runtime.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wctype.h>

renderer::~renderer()
// ----------------------------------------------------------------------------
//   When we used the scratchpad, free memory used
// ----------------------------------------------------------------------------
{
    if (!target)
        rt.free(written);
}


bool renderer::put(char c)
// ----------------------------------------------------------------------------
//   Write a single character
// ----------------------------------------------------------------------------
{
    if (sign)
    {
        sign = false;
        if (c != '-' && c != '+')
            put('+');
    }

    if (written >= length)
        return false;


    // Render flat for stack display: collect all spaces in one
    if (flat)
    {
        if (isspace(c))
        {
            if (space || cr)
                return true;
            c = ' ';
            space = true;
        }
        else
        {
            space = false;
        }
    }
    if (c == ' ' && (cr || nl))
    {
        cr = false;
        return true;
    }

    if (!isspace(c) && nl)
    {
        nl = false;
        if (!put('\n'))
            return false;
    }

    if (saving)
    {
        saving->put(c);
        written++;
    }
    else if (target)
    {
        target[written++] = c;
    }
    else
    {
        byte *p = rt.allocate(1);
        if (!p)
            return false;
        *p = c;
        written++;
    }
    if (c == '\n')
    {
        nl = false;
        if (!txt)
            for (uint i = 0; i < tabs; i++)
                if (!put('\t'))
                    return false;
        cr = true;
    }
    else if (!isspace(c))
    {
        cr = false;
    }

    if (c == '"')
        txt = !txt;
    return true;
}


bool renderer::put(settings::commands format, utf8 text)
// ----------------------------------------------------------------------------
//   Render a command with proper capitalization
// ----------------------------------------------------------------------------
{
    bool result = true;
    switch(format)
    {
    case settings::commands::LOWERCASE:
        for (utf8 s = text; *s; s = utf8_next(s))
            result = put(unicode(towlower(utf8_codepoint(s))));
        break;

    case settings::commands::UPPERCASE:
        for (utf8 s = text; *s; s = utf8_next(s))
            result = put(unicode(towupper(utf8_codepoint(s))));
        break;

    case settings::commands::CAPITALIZED:
        for (utf8 s = text; *s; s = utf8_next(s))
            result = put(unicode(s == text
                                 ? towupper(utf8_codepoint(s))
                                 : towlower(utf8_codepoint(s))));
        break;

    case settings::commands::LONG_FORM:
        for (cstring p = cstring(text); *p; p++)
            result = put(*p);
        break;
    }

    return result;
}



size_t renderer::printf(const char *format, ...)
// ----------------------------------------------------------------------------
//   Write a formatted string
// ----------------------------------------------------------------------------
{
    if (saving)
    {
        va_list va;
        va_start(va, format);
        char buf[80];
        size_t remaining = length - written;
        if (remaining > sizeof(buf))
            remaining = sizeof(buf);
        int size = vsnprintf(buf, remaining, format, va);
        va_end(va);
        if (size > 0)
            if (saving->write(buf, size))
                written += size;
        return size;
    }
    else if (target)
    {
        // Fixed target: write directly there
        if (written >= length)
            return 0;

        va_list va;
        va_start(va, format);
        size_t remaining = length - written;
        size_t size = vsnprintf(target + written, remaining, format, va);
        va_end(va);
        written += size;
        return size;
    }
    else
    {
        // Write in the scratchpad
        char buf[32];
        va_list va;
        va_start(va, format);
        size_t size = vsnprintf(buf, sizeof(buf), format, va);
        va_end(va);

        // Check if we can allocate enough for the output
        byte *p = rt.allocate(size);
        if (!p)
            return 0;

        if (size < sizeof(buf))
        {
            // Common case: it fits in 32-bytes buffer, allocate directly
            memcpy(p, buf, size);
        }
        else
        {
            // Uncommon case: re-run vsnprintf in the allocated buffer
            va_list va;
            va_start(va, format);
            size = vsnprintf((char *) p, size, format, va);
            va_end(va);
        }
        written += size;
        return size;
    }
}


utf8 renderer::text() const
// ----------------------------------------------------------------------------
//   Return the buffer of what was written in the renderer
// ----------------------------------------------------------------------------
{
    if (target)
        return (utf8) target;
    if (saving)
        return nullptr;
#ifdef SIMULATOR
    *rt.scratchpad() = 0;
#endif // SIMULATOR
    return (utf8) rt.scratchpad() - written;
}
