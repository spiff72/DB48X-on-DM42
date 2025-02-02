// ****************************************************************************
//  bignum.cc                                                    DB48X project
// ****************************************************************************
//
//   File Description:
//
//     Implementation of basic bignum operations
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

#include "bignum.h"

#include "fraction.h"
#include "integer.h"
#include "parser.h"
#include "renderer.h"
#include "runtime.h"
#include "settings.h"
#include "utf8.h"

#include <stdio.h>


RECORDER(bignum, 16, "Bignums");

bignum::bignum(id type, integer_g value)
// ----------------------------------------------------------------------------
//   Create a bignum from an integer value
// ----------------------------------------------------------------------------
    : text(type, value->payload(), bytesize(value))
{
    byte *p = (byte *) payload();
    size_t sz = leb128<size_t>(p);
    if (sz)
    {
        byte_p q = value->payload();
        uint c = 0;
        uint bits = 0;
        bool more;
        do
        {
            byte b = *q++;
            more = b & 0x80;
            c |= (b & 0x7F) << bits;
            bits += 7;
            if (bits >= 8)
            {
                *p++ = byte(c);
                c >>= 8;
                bits -= 8;
            }
        } while (more);
        if (c)
            *p++ = byte(c);
    }
}


size_t bignum::required_memory(id i, integer_g value)
// ----------------------------------------------------------------------------
//   Compute the size to copy an integer value
// ----------------------------------------------------------------------------
{
    size_t size = bytesize(value);
    return leb128size(i) + leb128size(size) + size;
}


integer_p bignum::as_integer() const
// ----------------------------------------------------------------------------
//   Check if this fits in a small integer, if so build it
// ----------------------------------------------------------------------------
{
    size_t size = 0;
    byte_p p = value(&size);
    if (size > sizeof(ularge))
        return nullptr;
    ularge value = 0;
    for (uint i = 0; i < size; i++)
        value |= ularge(p[i]) << (i * 8);
    id ty = type() == ID_neg_bignum ? ID_neg_integer : ID_integer;
    return rt.make<integer>(ty, value);
}


PARSE_BODY(bignum)
// ----------------------------------------------------------------------------
//    Bignums are parsed by integer parser, so we can skip here
// ----------------------------------------------------------------------------
{
    return SKIP;
}


HELP_BODY(bignum)
// ----------------------------------------------------------------------------
//    Help topic for big integers
// ----------------------------------------------------------------------------
{
    return utf8("Big integers");
}


static size_t render_num(renderer &r,
                         bignum_p  num,
                         uint      base,
                         cstring   fmt,
                         bool      raw = false)
// ----------------------------------------------------------------------------
//   Convert an bignum value to the proper format
// ----------------------------------------------------------------------------
//   This is necessary because the arm-none-eabi-gcc printf can't do 64-bit
//   I'm getting non-sensible output
{
    // If we render to a file, need to first render to scratchpad to be able to
    // revert the digits in memory before writing
    if (r.file_save())
    {
        renderer tmp(r.equation(), r.editing(), r.stack());
        size_t result = render_num(tmp, num, base, fmt, true);
        r.put(tmp.text(), result);
        return result;
    }

    // Check which kind of spacing to use
    bool based = *fmt == '#';
    bool fancy_base = based && r.stack();
    uint spacing = based ? Settings.spacing_based : Settings.spacing_mantissa;
    unicode space = based ? Settings.space_based : Settings.space;
    if (raw)
    {
        fancy_base = false;
        spacing = 0;
        space = 0;
    }

    // Copy the '#' or '-' sign
    if (*fmt)
        r.put(*fmt++);
    else
        r.flush();

    // Get denominator for the base
    size_t findex = r.size();
    bignum_g b = rt.make<bignum>(object::ID_bignum, base);
    bignum_g n = (bignum *) num;

    // Keep dividing by the base until we get 0
    uint sep = 0;
    do
    {
        bignum_g remainder = nullptr;
        bignum_g quotient = nullptr;
        if (!bignum::quorem(n, b, bignum::ID_bignum, &quotient, &remainder))
            break;
        uint digit = remainder->value<uint>();
        if (digit > base)
        {
            printf("Ooops: digit=%u, base=%u\n", digit, base);
            bignum::quorem(n, b, bignum::ID_bignum, &quotient, &remainder);
        }
        char c = (digit < 10) ? digit + '0' : digit + ('A' - 10);
        r.put(c);
        n = quotient;

        if (!n->is_zero() && ++sep == spacing)
        {
            sep = 0;
            r.put(space);
        }
    } while (!n->is_zero());

    // Revert the digits
    byte *dest  = (byte *) r.text();
    bool multibyte = spacing && space > 0xFF;
    utf8_reverse(dest + findex, dest + r.size(), multibyte);

    // Add suffix if there is one
    if (fancy_base)
    {
        static uint16_t fancy_base_digits[10] =
        {
                L'₀', L'₁', L'₂', L'₃', L'₄',
                L'₅', L'₆', L'₇', L'₈', L'₉'
        };
        if (base / 10)
            r.put(unicode(fancy_base_digits[base/10]));
        r.put(unicode(fancy_base_digits[base%10]));
    }
    else if (*fmt)
        r.put(*fmt++);

    // Return the number of items we need
    return r.size();
}


RENDER_BODY(bignum)
// ----------------------------------------------------------------------------
//   Render the bignum into the given string buffer
// ----------------------------------------------------------------------------
{
    size_t result = render_num(r, o, 10, "");
    return result;
}


template<>
RENDER_BODY(neg_bignum)
// ----------------------------------------------------------------------------
//   Render the negative bignum value into the given string buffer
// ----------------------------------------------------------------------------
{
    return render_num(r, o, 10, "-");
}


#if CONFIG_FIXED_BASED_OBJECTS
template<>
RENDER_BODY(hex_bignum)
// ----------------------------------------------------------------------------
//   Render the hexadecimal bignum value into the given string buffer
// ----------------------------------------------------------------------------
{
    return render_num(r, o, 16, "#h");
}

template<>
RENDER_BODY(dec_bignum)
// ----------------------------------------------------------------------------
//   Render the decimal based number
// ----------------------------------------------------------------------------
{
    return render_num(r, o, 10, "#d");
}

template<>
RENDER_BODY(oct_bignum)
// ----------------------------------------------------------------------------
//   Render the octal bignum value into the given string buffer
// ----------------------------------------------------------------------------
{
    return render_num(r, o, 8, "#o");
}

template<>
RENDER_BODY(bin_bignum)
// ----------------------------------------------------------------------------
//   Render the binary bignum value into the given string buffer
// ----------------------------------------------------------------------------
{
    return render_num(r, o, 2, "#b");
}
#endif // CONFIG_FIXED_BASED_OBJECTS


template<>
RENDER_BODY(based_bignum)
// ----------------------------------------------------------------------------
//   Render the hexadecimal bignum value into the given string buffer
// ----------------------------------------------------------------------------
{
    return render_num(r, o, Settings.base, "#");
}


// ============================================================================
//
//    Big bignum comparisons
//
// ============================================================================

int bignum::compare(bignum_r xg, bignum_r yg, bool magnitude)
// ----------------------------------------------------------------------------
//   Compare two bignum values
// ----------------------------------------------------------------------------
{
    id xt = xg->type();
    id yt = yg->type();

    // Negative bignums are always smaller than positive bignums
    if (!magnitude)
    {
        if (xt == ID_neg_bignum && yt != ID_neg_bignum)
            return -1;
        else if (yt == ID_neg_bignum && xt != ID_neg_bignum)
            return 1;
    }

    size_t xs = 0;
    size_t ys = 0;
    byte_p x = xg->value(&xs);
    byte_p y = yg->value(&ys);

    // First check if size difference is sufficient to let us decide
    int result = xs - ys;
    if (!result)
    {
        // Compare, starting with highest order
        for (int i = xs - 1; !result && i >= 0; i--)
            result = x[i] - y[i];
    }

    // If xt is ID_neg_bignum, then yt also must be, see test at top of function
    if (!magnitude && xt == ID_neg_bignum)
        result = -result;
    return result;
}



// ============================================================================
//
//    Big bignum arithmetic
//
// ============================================================================

// Operations with carry
static inline uint16_t add_op(byte x, byte y, byte c) { return x + y + (c != 0);}
static inline uint16_t sub_op(byte x, byte y, byte c) { return x - y - (c != 0);}
static inline uint16_t neg_op(byte x, byte c)         { return -x - (c != 0); }
static inline byte     not_op(byte x, byte  )         { return ~x; }
static inline byte     and_op(byte x, byte y, byte  ) { return x & y; }
static inline byte     or_op (byte x, byte y, byte  ) { return x | y; }
static inline byte     xor_op(byte x, byte y, byte  ) { return x ^ y; }


inline object::id bignum::opposite_type(id type)
// ----------------------------------------------------------------------------
//   Return the type of the opposite
// ----------------------------------------------------------------------------
{
    switch(type)
    {
    case ID_bignum:             return ID_neg_bignum;
    case ID_neg_bignum:         return ID_bignum;
    default:                    return type;
    }
}


bignum_g operator-(bignum_r xg)
// ----------------------------------------------------------------------------
//   Negate the input value
// ----------------------------------------------------------------------------
{
    object::id xt = xg->type();
    size_t xs = 0;
    byte_p x = xg->value(&xs);

    // Deal with simple case where we can simply copy the payload
    if (xt == object::ID_bignum)
        return rt.make<bignum>(object::ID_neg_bignum, x, xs);
    else if (xt == object::ID_neg_bignum)
        return rt.make<bignum>(object::ID_bignum, x, xs);

    // Complicated case of based numbers: need to actually compute the opposite
    return bignum::unary<true>(neg_op, xg);
}


bignum_g operator~(bignum_r x)
// ----------------------------------------------------------------------------
//   Boolean not
// ----------------------------------------------------------------------------
{
    object::id xt = x->type();

    // For bignum and neg_bignum, do a 0/1 logical not
    if (xt == object::ID_bignum || xt == object::ID_neg_bignum)
        return rt.make<bignum>(object::ID_bignum, x->is_zero());

    // For hex_bignum and other based numbers, do a binary not
    return bignum::unary<true>(not_op, x);
}


bignum_g bignum::add_sub(bignum_r yg, bignum_r xg, bool issub)
// ----------------------------------------------------------------------------
//   Add the two bignum values, result has type of x
// ----------------------------------------------------------------------------
{
    if (!xg.Safe() || !yg.Safe())
        return nullptr;

    id yt = yg->type();
    id xt = xg->type();

    // Check if we have opposite signs
    bool samesgn = (xt == ID_neg_bignum) == (yt == ID_neg_bignum);
    if (samesgn == issub)
    {
        int cmp = compare(yg, xg, true);
        if (cmp >= 0)
        {
            // abs Y > abs X: result has opposite type of X
            id ty = cmp == 0 ? ID_bignum: issub ? xt : opposite_type(xt);
            return binary<false>(sub_op, yg, xg, ty);
        }
        else
        {
            // abs Y < abs X: result has type of X
            id ty = issub ? opposite_type(xt) : xt;
            return binary<false>(sub_op, xg, yg, ty);
        }
    }

    // We have the same sign, add items
    id ty = issub ? opposite_type(xt) : xt;
    return binary<false>(add_op, yg, xg, ty);
}


bignum_g operator+(bignum_r y, bignum_r x)
// ----------------------------------------------------------------------------
//   Add the two bignum values, result has type of x
// ----------------------------------------------------------------------------
{
    return bignum::add_sub(y, x, false);
}


bignum_g operator-(bignum_r y, bignum_r x)
// ----------------------------------------------------------------------------
//   Subtract two bignum values, result has type of x
// ----------------------------------------------------------------------------
{
    return bignum::add_sub(y, x, true);
}


bignum_g operator&(bignum_r y, bignum_r x)
// ----------------------------------------------------------------------------
//   Perform a binary and operation
// ----------------------------------------------------------------------------
{
    return bignum::binary<false>(and_op, x, y, x->type());
}


bignum_g operator|(bignum_r y, bignum_r x)
// ----------------------------------------------------------------------------
//   Perform a binary or operation
// ----------------------------------------------------------------------------
{
    return bignum::binary<false>(or_op, x, y, x->type());
}


bignum_g operator^(bignum_r y, bignum_r x)
// ----------------------------------------------------------------------------
//   Perform a binary xor operation
// ----------------------------------------------------------------------------
{
    return bignum::binary<false>(xor_op, x, y, x->type());
}


bignum_g bignum::multiply(bignum_r yg, bignum_r xg, id ty)
// ----------------------------------------------------------------------------
//   Perform multiply operation on the two big nums, with result type ty
// ----------------------------------------------------------------------------
{
    size_t xs = 0;
    size_t ys = 0;
    byte_p x = xg->value(&xs);                // Read sizes and pointers
    byte_p y = yg->value(&ys);
    id xt = xg->type();
    size_t wbits = wordsize(xt);
    size_t wbytes = (wbits + 7) / 8;
    size_t needed = xs + ys;
    if (needed * 8 > Settings.maxbignum)
    {
        rt.number_too_big_error();
        return nullptr;
    }
    if (wbits && needed > wbytes)
        needed = wbytes;
    byte *buffer = rt.allocate(needed);       // May GC here
    if (!buffer)
        return nullptr;                       // Out of memory
    x = xg->value(&xs);                       // Re-read after potential GC
    y = yg->value(&ys);

    // Zero-initialie the result
    for (size_t i = 0; i < needed; i++)
        buffer[i] = 0;

    // Loop on all bytes of x then y
    for (size_t xi = 0; xi < xs; xi++)
    {
        byte xd = x[xi];
        for (int bit = 0; xd && bit < 8; bit++)
        {
            if (xd & (1<<bit))
            {
                uint c = 0;
                size_t yi;
                for (yi = 0; yi < ys && xi + yi < needed; yi++)
                {
                    c += buffer[xi + yi] + (y[yi] << bit);
                    buffer[xi + yi] = byte(c);
                    c >>= 8;
                }
                while (c && xi + yi < needed)
                {
                    c += buffer[xi + yi];
                    buffer[xi + yi] = byte(c);
                    c >>= 8;
                    yi++;
                }
                xd &= ~(1<<bit);
            }
        }
    }

    size_t sz = needed;
    while (sz > 0 && buffer[sz-1] == 0)
        sz--;
    gcbytes buf = buffer;
    bignum_g result = rt.make<bignum>(ty, buf, sz);
    rt.free(needed);
    return result;
}


bignum_g operator*(bignum_r y, bignum_r x)
// ----------------------------------------------------------------------------
//   Multiplication of bignums
// ----------------------------------------------------------------------------
{
    if (!x.Safe() || !y.Safe())
        return nullptr;
    object::id xt = x->type();
    object::id yt = y->type();
    object::id prodtype = bignum::product_type(yt, xt);
    return bignum::multiply(y, x, prodtype);
}


bool bignum::quorem(bignum_r yg, bignum_r xg, id ty, bignum_g *q, bignum_g *r)
// ----------------------------------------------------------------------------
//   Compute quotient and remainder of two bignums, as bignums
// ----------------------------------------------------------------------------
//   Result is placed in scratchpad, the function returns the size in bytes
{
    if (xg->is_zero())
    {
        rt.zero_divide_error();
        return false;
    }

    // In the computations below (e.g. needed), the size of the quotient is
    // less than the size of y, and the size of the remainder is less than the
    // size of x, therefore, we need at most xs + ys for both.
    // However, the computation of the remainder requires a subtraction
    // which can be one byte larger than x (see issue #70 for details).
    // For example in 0x17B/0xEF, the first remainder subtraction will be
    // 0x17B - 0xEF, which does require two bytes, not just one. It could
    // be carried out by keeping the extra byte in a variable, but it's
    // simpler to allocate one extra byte.
    size_t xs = 0;
    size_t ys = 0;
    byte_p x = xg->value(&xs);                // Read those after potential GC
    byte_p y = yg->value(&ys);
    id xt = xg->type();
    size_t wbits = wordsize(xt);
    size_t wbytes = (wbits + 7) / 8;
    size_t needed = ys + xs + 1;              // No need to check maxbignum
    byte *buffer = rt.allocate(needed);       // May GC here
    if (!buffer)
        return false;                         // Out of memory
    x = xg->value(&xs);                       // Re-read after potential GC
    y = yg->value(&ys);

    // Pointers ot quotient and remainder, initializer to 0
    byte *quotient = buffer;
    byte *remainder = quotient + ys;
    size_t rs = 0;
    size_t qs = 0;
    for (uint i = 0; i < needed; i++)
        buffer[i] = 0;

    // Loop on the numerator
    for (int yi = ys-1; yi >= 0; yi--)
    {
        for (int bit = 7; bit >= 0; bit--)
        {
            // Shift remainder left by one bit, add numerator bit
            uint16_t c = (y[yi] >> bit) & 1;
            int delta = 0;
            for (uint ri = 0; ri < rs; ri++)
            {
                c += (remainder[ri] << 1);
                remainder[ri] = byte(c);
                if (int d = remainder[ri] - x[ri])
                    delta = d;
                c >>= 8;
            }

            if (c)
            {
                if (int d = c - x[rs])
                    delta = d;
                remainder[rs++] = c;
            }
            if (rs != xs)
                delta = int(rs) - int(xs);

            // If remainder >= denominator, add to quotient, subtract from rem
            if (delta >= 0)
            {
                quotient[yi] |= (1 << bit);
                if (qs < size_t(yi + 1))
                    qs = yi + 1;

                c = 0;
                for (uint ri = 0; ri < rs; ri++)
                {
                    c = remainder[ri] - (ri < xs ? x[ri] : 0) - c;
                    remainder[ri] = byte(c);
                    c = c > 0xFF;
                }

                // Strip zeroes at top of remainder
                while (rs > 0 && remainder[rs-1] == 0)
                    rs--;
            }
        } // numerator bit loop
    } // numerator byte loop

    // Generate results
    gcutf8 qg = quotient;
    gcutf8 rg = remainder;
    bool ok = true;
    if (q)
    {
        if (wbits && qs > wbytes)
            qs = wbytes;
        *q = rt.make<bignum>(ty, qg, qs);
        ok = bignum_p(*q) != nullptr;
    }
    if (r && ok)
    {
        if (wbits && rs > wbytes)
            rs = wbytes;
        *r = rt.make<bignum>(ty, rg, rs);
        ok = bignum_p(*r) != nullptr;
    }
    rt.free(needed);
    return ok;
}


bignum_g operator/(bignum_r y, bignum_r x)
// ----------------------------------------------------------------------------
//   Perform long division of y by x
// ----------------------------------------------------------------------------
{
    if (!x.Safe() || !y.Safe())
        return nullptr;
    object::id yt = y->type();
    object::id xt = x->type();
    object::id prodtype = bignum::product_type(yt, xt);

    bignum_g q = nullptr;
    bignum::quorem(y, x, prodtype, &q, nullptr);
    return q;
}


bignum_g operator%(bignum_r y, bignum_r x)
// ----------------------------------------------------------------------------
//  Perform long-remainder of y by x
// ----------------------------------------------------------------------------
{
    if (!x.Safe() || !y.Safe())
        return nullptr;
    object::id yt = y->type();
    bignum_g r = nullptr;
    bignum::quorem(y, x, yt, nullptr, &r);
    return r;
}


bignum_g bignum::pow(bignum_r yr, bignum_r xr)
// ----------------------------------------------------------------------------
//    Compute y^abs(x)
// ----------------------------------------------------------------------------
//   Note that the case where x is negative should be filtered by caller
{
    if (!xr.Safe() || !yr.Safe())
        return nullptr;
    bignum_g r  = bignum::make(1);
    size_t   xs = 0;
    byte_p   x  = xr->value(&xs);
    bignum_g y  = yr;

    for (size_t xi = 0; xi < xs; xi++)
    {
        byte xv = x[xi];
        for (uint bit = 0; xv && bit < 8; bit++)
        {
            if (xv & 1)
                r = r * y;
            xv >>= 1;
            if (xv || xi < xs-1)
                y = y * y;
        }
    }
    return r;
}


RENDER_BODY(big_fraction)
// ----------------------------------------------------------------------------
//   Render the fraction as 'num/den'
// ----------------------------------------------------------------------------
{
    bignum_g n = o->numerator();
    bignum_g d = o->denominator();
    render_num(r, n, 10, "");
    r.put('/');
    render_num(r, d, 10, "");
    return r.size();
}


RENDER_BODY(neg_big_fraction)
// ----------------------------------------------------------------------------
//   Render the fraction as '-num/den'
// ----------------------------------------------------------------------------
{
    bignum_g n = o->numerator();
    bignum_g d = o->denominator();
    render_num(r, n, 10, "-/");
    render_num(r, d, 10, "");
    return r.size();
}
