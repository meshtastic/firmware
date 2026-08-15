#include "PinSketch.h"

#include <algorithm>

namespace pinsketch
{

// Low-order terms of x^32 + x^7 + x^3 + x^2 + 1.
static constexpr Element MODULUS = 0x8D;
static constexpr int FIELD_BITS = 32;

Element mul(Element a, Element b)
{
    Element r = 0;
    while (b) {
        if (b & 1)
            r ^= a;
        b >>= 1;
        const bool overflow = a & 0x80000000u;
        a <<= 1;
        if (overflow)
            a ^= MODULUS;
    }
    return r;
}

Element sqr(Element a)
{
    return mul(a, a);
}

Element inv(Element a)
{
    // a^(2^32-2), the exponent being the sum of 2^1..2^31.
    Element r = 1, p = a;
    for (int i = 1; i < FIELD_BITS; i++) {
        p = sqr(p);
        r = mul(r, p);
    }
    return r;
}

// --- Polynomials over GF(2^32), coefficient index == degree, no trailing zeros ---

using Poly = std::vector<Element>;

static void polyTrim(Poly &p)
{
    while (!p.empty() && p.back() == 0)
        p.pop_back();
}

// Degree of the zero polynomial is reported as -1.
static int polyDeg(const Poly &p)
{
    return (int)p.size() - 1;
}

static void polyAddTo(Poly &a, const Poly &b)
{
    if (a.size() < b.size())
        a.resize(b.size(), 0);
    for (size_t i = 0; i < b.size(); i++)
        a[i] ^= b[i];
    polyTrim(a);
}

static Poly polyMul(const Poly &a, const Poly &b)
{
    if (a.empty() || b.empty())
        return Poly();
    Poly r(a.size() + b.size() - 1, 0);
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i] == 0)
            continue;
        for (size_t j = 0; j < b.size(); j++)
            r[i + j] ^= mul(a[i], b[j]);
    }
    polyTrim(r);
    return r;
}

// Long division, returning the remainder and optionally the quotient.
static Poly polyDivMod(const Poly &num, const Poly &den, Poly *quotient = nullptr)
{
    if (quotient)
        quotient->clear();
    if (den.empty())
        return Poly();

    Poly rem = num;
    polyTrim(rem);
    const int dd = polyDeg(den);
    const Element leadInv = inv(den.back());
    if (quotient && polyDeg(rem) >= dd)
        quotient->assign(polyDeg(rem) - dd + 1, 0);

    while (polyDeg(rem) >= dd && !rem.empty()) {
        const int shift = polyDeg(rem) - dd;
        const Element coef = mul(rem.back(), leadInv);
        if (quotient)
            (*quotient)[shift] = coef;
        for (int i = 0; i <= dd; i++)
            rem[shift + i] ^= mul(coef, den[i]);
        polyTrim(rem);
    }
    if (quotient)
        polyTrim(*quotient);
    return rem;
}

static Poly polyGcd(Poly a, Poly b)
{
    polyTrim(a);
    polyTrim(b);
    while (!b.empty()) {
        Poly r = polyDivMod(a, b);
        a = b;
        b = r;
    }
    return a;
}

// Tr(beta*z) reduced mod f, where Tr(y) = y + y^2 + y^4 + ... + y^(2^31).
static Poly tracePoly(Element beta, const Poly &f)
{
    Poly h = polyDivMod(Poly{0, beta}, f);
    Poly acc = h;
    for (int i = 1; i < FIELD_BITS; i++) {
        h = polyDivMod(polyMul(h, h), f);
        polyAddTo(acc, h);
    }
    return acc;
}

/**
 * Berlekamp trace algorithm. Splits f by Tr(beta*x), which separates any two distinct roots for at
 * least one basis element beta, then recurses on both factors. Returns false if f does not split
 * completely over the field, which is how an invalid locator is caught.
 */
static bool findRoots(const Poly &f, std::vector<Element> &out)
{
    const int deg = polyDeg(f);
    if (deg <= 0)
        return deg == 0; // a nonzero constant has no roots; the zero polynomial is not decodable
    if (deg == 1) {
        out.push_back(mul(f[0], inv(f[1])));
        return true;
    }
    for (int k = 0; k < FIELD_BITS; k++) {
        const Poly g = polyGcd(f, tracePoly((Element)1u << k, f));
        const int dg = polyDeg(g);
        if (dg > 0 && dg < deg) {
            Poly q;
            polyDivMod(f, g, &q);
            return findRoots(g, out) && findRoots(q, out);
        }
    }
    return false;
}

/**
 * Berlekamp-Massey over the syndrome sequence, yielding the shortest connection polynomial. For a
 * difference set of size d <= capacity that polynomial is the error locator, whose roots are the
 * inverses of the set members.
 */
static Poly berlekampMassey(const std::vector<Element> &s)
{
    Poly c{1}, b{1};
    size_t l = 0, m = 1;
    Element bb = 1;

    for (size_t n = 0; n < s.size(); n++) {
        Element d = s[n];
        for (size_t i = 1; i <= l && i < c.size(); i++)
            d ^= mul(c[i], s[n - i]);
        if (d == 0) {
            m++;
            continue;
        }
        const Poly prev = c;
        const Element coef = mul(d, inv(bb));
        if (c.size() < b.size() + m)
            c.resize(b.size() + m, 0);
        for (size_t i = 0; i < b.size(); i++)
            c[i + m] ^= mul(coef, b[i]);
        if (2 * l <= n) {
            l = n + 1 - l;
            b = prev;
            bb = d;
            m = 1;
        } else {
            m++;
        }
    }
    polyTrim(c);
    return c;
}

// --- Sketch ---

bool Sketch::add(Element e)
{
    if (e == 0)
        return false;
    const Element step = sqr(e);
    Element power = e;
    for (size_t i = 0; i < syndromes.size(); i++) {
        syndromes[i] ^= power;
        power = mul(power, step);
    }
    return true;
}

bool Sketch::merge(const Sketch &other)
{
    if (other.syndromes.size() != syndromes.size())
        return false;
    for (size_t i = 0; i < syndromes.size(); i++)
        syndromes[i] ^= other.syndromes[i];
    return true;
}

bool Sketch::empty() const
{
    for (Element s : syndromes)
        if (s != 0)
            return false;
    return true;
}

void Sketch::clear()
{
    std::fill(syndromes.begin(), syndromes.end(), 0);
}

void Sketch::truncate(size_t newCapacity)
{
    if (newCapacity < syndromes.size())
        syndromes.resize(newCapacity);
}

void Sketch::serialize(uint8_t *out) const
{
    for (size_t i = 0; i < syndromes.size(); i++) {
        const Element e = syndromes[i];
        out[i * 4 + 0] = (uint8_t)(e & 0xFF);
        out[i * 4 + 1] = (uint8_t)((e >> 8) & 0xFF);
        out[i * 4 + 2] = (uint8_t)((e >> 16) & 0xFF);
        out[i * 4 + 3] = (uint8_t)((e >> 24) & 0xFF);
    }
}

bool Sketch::deserialize(const uint8_t *in, size_t len)
{
    if (len % sizeof(Element) != 0)
        return false;
    syndromes.assign(len / sizeof(Element), 0);
    for (size_t i = 0; i < syndromes.size(); i++)
        syndromes[i] = (Element)in[i * 4 + 0] | ((Element)in[i * 4 + 1] << 8) | ((Element)in[i * 4 + 2] << 16) |
                       ((Element)in[i * 4 + 3] << 24);
    return true;
}

bool Sketch::decode(std::vector<Element> &out) const
{
    out.clear();
    if (syndromes.empty())
        return false;
    if (empty())
        return true;

    // Odd syndromes are carried; even ones are free, since S_2j == S_j^2 in characteristic 2.
    const size_t c = syndromes.size();
    std::vector<Element> s(2 * c, 0);
    for (size_t j = 1; j <= 2 * c; j++)
        s[j - 1] = (j & 1) ? syndromes[(j - 1) / 2] : sqr(s[j / 2 - 1]);

    const Poly locator = berlekampMassey(s);
    if (locator.empty() || locator[0] != 1 || (size_t)polyDeg(locator) > c)
        return false;

    std::vector<Element> roots;
    if (!findRoots(locator, roots) || roots.size() != (size_t)polyDeg(locator))
        return false;

    std::vector<Element> found;
    found.reserve(roots.size());
    for (Element r : roots) {
        if (r == 0)
            return false;
        found.push_back(inv(r));
    }
    std::sort(found.begin(), found.end());
    if (std::adjacent_find(found.begin(), found.end()) != found.end())
        return false;

    // The locator is only a candidate until the recovered set reproduces the syndromes we started
    // from. Without this a difference larger than the capacity can decode to a plausible wrong set.
    Sketch check(c);
    for (Element e : found)
        if (!check.add(e))
            return false;
    if (check.syndromes != syndromes)
        return false;

    out = std::move(found);
    return true;
}

} // namespace pinsketch
