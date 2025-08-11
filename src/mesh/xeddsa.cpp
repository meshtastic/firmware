#include "xeddsa.h"

static crypto_uint64 load_3(const unsigned char *in)
{
    crypto_uint64 result;
    result = (crypto_uint64)in[0];
    result |= ((crypto_uint64)in[1]) << 8;
    result |= ((crypto_uint64)in[2]) << 16;
    return result;
}

static crypto_uint64 load_4(const unsigned char *in)
{
    crypto_uint64 result;
    result = (crypto_uint64)in[0];
    result |= ((crypto_uint64)in[1]) << 8;
    result |= ((crypto_uint64)in[2]) << 16;
    result |= ((crypto_uint64)in[3]) << 24;
    return result;
}

void sc_muladd(unsigned char *s, const unsigned char *a, const unsigned char *b, const unsigned char *c)
{
    crypto_int64 a0 = 2097151 & load_3(a);
    crypto_int64 a1 = 2097151 & (load_4(a + 2) >> 5);
    crypto_int64 a2 = 2097151 & (load_3(a + 5) >> 2);
    crypto_int64 a3 = 2097151 & (load_4(a + 7) >> 7);
    crypto_int64 a4 = 2097151 & (load_4(a + 10) >> 4);
    crypto_int64 a5 = 2097151 & (load_3(a + 13) >> 1);
    crypto_int64 a6 = 2097151 & (load_4(a + 15) >> 6);
    crypto_int64 a7 = 2097151 & (load_3(a + 18) >> 3);
    crypto_int64 a8 = 2097151 & load_3(a + 21);
    crypto_int64 a9 = 2097151 & (load_4(a + 23) >> 5);
    crypto_int64 a10 = 2097151 & (load_3(a + 26) >> 2);
    crypto_int64 a11 = (load_4(a + 28) >> 7);
    crypto_int64 b0 = 2097151 & load_3(b);
    crypto_int64 b1 = 2097151 & (load_4(b + 2) >> 5);
    crypto_int64 b2 = 2097151 & (load_3(b + 5) >> 2);
    crypto_int64 b3 = 2097151 & (load_4(b + 7) >> 7);
    crypto_int64 b4 = 2097151 & (load_4(b + 10) >> 4);
    crypto_int64 b5 = 2097151 & (load_3(b + 13) >> 1);
    crypto_int64 b6 = 2097151 & (load_4(b + 15) >> 6);
    crypto_int64 b7 = 2097151 & (load_3(b + 18) >> 3);
    crypto_int64 b8 = 2097151 & load_3(b + 21);
    crypto_int64 b9 = 2097151 & (load_4(b + 23) >> 5);
    crypto_int64 b10 = 2097151 & (load_3(b + 26) >> 2);
    crypto_int64 b11 = (load_4(b + 28) >> 7);
    crypto_int64 c0 = 2097151 & load_3(c);
    crypto_int64 c1 = 2097151 & (load_4(c + 2) >> 5);
    crypto_int64 c2 = 2097151 & (load_3(c + 5) >> 2);
    crypto_int64 c3 = 2097151 & (load_4(c + 7) >> 7);
    crypto_int64 c4 = 2097151 & (load_4(c + 10) >> 4);
    crypto_int64 c5 = 2097151 & (load_3(c + 13) >> 1);
    crypto_int64 c6 = 2097151 & (load_4(c + 15) >> 6);
    crypto_int64 c7 = 2097151 & (load_3(c + 18) >> 3);
    crypto_int64 c8 = 2097151 & load_3(c + 21);
    crypto_int64 c9 = 2097151 & (load_4(c + 23) >> 5);
    crypto_int64 c10 = 2097151 & (load_3(c + 26) >> 2);
    crypto_int64 c11 = (load_4(c + 28) >> 7);
    crypto_int64 s0;
    crypto_int64 s1;
    crypto_int64 s2;
    crypto_int64 s3;
    crypto_int64 s4;
    crypto_int64 s5;
    crypto_int64 s6;
    crypto_int64 s7;
    crypto_int64 s8;
    crypto_int64 s9;
    crypto_int64 s10;
    crypto_int64 s11;
    crypto_int64 s12;
    crypto_int64 s13;
    crypto_int64 s14;
    crypto_int64 s15;
    crypto_int64 s16;
    crypto_int64 s17;
    crypto_int64 s18;
    crypto_int64 s19;
    crypto_int64 s20;
    crypto_int64 s21;
    crypto_int64 s22;
    crypto_int64 s23;
    crypto_int64 carry0;
    crypto_int64 carry1;
    crypto_int64 carry2;
    crypto_int64 carry3;
    crypto_int64 carry4;
    crypto_int64 carry5;
    crypto_int64 carry6;
    crypto_int64 carry7;
    crypto_int64 carry8;
    crypto_int64 carry9;
    crypto_int64 carry10;
    crypto_int64 carry11;
    crypto_int64 carry12;
    crypto_int64 carry13;
    crypto_int64 carry14;
    crypto_int64 carry15;
    crypto_int64 carry16;
    crypto_int64 carry17;
    crypto_int64 carry18;
    crypto_int64 carry19;
    crypto_int64 carry20;
    crypto_int64 carry21;
    crypto_int64 carry22;

    s0 = c0 + a0 * b0;
    s1 = c1 + a0 * b1 + a1 * b0;
    s2 = c2 + a0 * b2 + a1 * b1 + a2 * b0;
    s3 = c3 + a0 * b3 + a1 * b2 + a2 * b1 + a3 * b0;
    s4 = c4 + a0 * b4 + a1 * b3 + a2 * b2 + a3 * b1 + a4 * b0;
    s5 = c5 + a0 * b5 + a1 * b4 + a2 * b3 + a3 * b2 + a4 * b1 + a5 * b0;
    s6 = c6 + a0 * b6 + a1 * b5 + a2 * b4 + a3 * b3 + a4 * b2 + a5 * b1 + a6 * b0;
    s7 = c7 + a0 * b7 + a1 * b6 + a2 * b5 + a3 * b4 + a4 * b3 + a5 * b2 + a6 * b1 + a7 * b0;
    s8 = c8 + a0 * b8 + a1 * b7 + a2 * b6 + a3 * b5 + a4 * b4 + a5 * b3 + a6 * b2 + a7 * b1 + a8 * b0;
    s9 = c9 + a0 * b9 + a1 * b8 + a2 * b7 + a3 * b6 + a4 * b5 + a5 * b4 + a6 * b3 + a7 * b2 + a8 * b1 + a9 * b0;
    s10 = c10 + a0 * b10 + a1 * b9 + a2 * b8 + a3 * b7 + a4 * b6 + a5 * b5 + a6 * b4 + a7 * b3 + a8 * b2 + a9 * b1 + a10 * b0;
    s11 = c11 + a0 * b11 + a1 * b10 + a2 * b9 + a3 * b8 + a4 * b7 + a5 * b6 + a6 * b5 + a7 * b4 + a8 * b3 + a9 * b2 + a10 * b1 +
          a11 * b0;
    s12 = a1 * b11 + a2 * b10 + a3 * b9 + a4 * b8 + a5 * b7 + a6 * b6 + a7 * b5 + a8 * b4 + a9 * b3 + a10 * b2 + a11 * b1;
    s13 = a2 * b11 + a3 * b10 + a4 * b9 + a5 * b8 + a6 * b7 + a7 * b6 + a8 * b5 + a9 * b4 + a10 * b3 + a11 * b2;
    s14 = a3 * b11 + a4 * b10 + a5 * b9 + a6 * b8 + a7 * b7 + a8 * b6 + a9 * b5 + a10 * b4 + a11 * b3;
    s15 = a4 * b11 + a5 * b10 + a6 * b9 + a7 * b8 + a8 * b7 + a9 * b6 + a10 * b5 + a11 * b4;
    s16 = a5 * b11 + a6 * b10 + a7 * b9 + a8 * b8 + a9 * b7 + a10 * b6 + a11 * b5;
    s17 = a6 * b11 + a7 * b10 + a8 * b9 + a9 * b8 + a10 * b7 + a11 * b6;
    s18 = a7 * b11 + a8 * b10 + a9 * b9 + a10 * b8 + a11 * b7;
    s19 = a8 * b11 + a9 * b10 + a10 * b9 + a11 * b8;
    s20 = a9 * b11 + a10 * b10 + a11 * b9;
    s21 = a10 * b11 + a11 * b10;
    s22 = a11 * b11;
    s23 = 0;

    carry0 = (s0 + (1 << 20)) >> 21;
    s1 += carry0;
    s0 -= carry0 << 21;
    carry2 = (s2 + (1 << 20)) >> 21;
    s3 += carry2;
    s2 -= carry2 << 21;
    carry4 = (s4 + (1 << 20)) >> 21;
    s5 += carry4;
    s4 -= carry4 << 21;
    carry6 = (s6 + (1 << 20)) >> 21;
    s7 += carry6;
    s6 -= carry6 << 21;
    carry8 = (s8 + (1 << 20)) >> 21;
    s9 += carry8;
    s8 -= carry8 << 21;
    carry10 = (s10 + (1 << 20)) >> 21;
    s11 += carry10;
    s10 -= carry10 << 21;
    carry12 = (s12 + (1 << 20)) >> 21;
    s13 += carry12;
    s12 -= carry12 << 21;
    carry14 = (s14 + (1 << 20)) >> 21;
    s15 += carry14;
    s14 -= carry14 << 21;
    carry16 = (s16 + (1 << 20)) >> 21;
    s17 += carry16;
    s16 -= carry16 << 21;
    carry18 = (s18 + (1 << 20)) >> 21;
    s19 += carry18;
    s18 -= carry18 << 21;
    carry20 = (s20 + (1 << 20)) >> 21;
    s21 += carry20;
    s20 -= carry20 << 21;
    carry22 = (s22 + (1 << 20)) >> 21;
    s23 += carry22;
    s22 -= carry22 << 21;

    carry1 = (s1 + (1 << 20)) >> 21;
    s2 += carry1;
    s1 -= carry1 << 21;
    carry3 = (s3 + (1 << 20)) >> 21;
    s4 += carry3;
    s3 -= carry3 << 21;
    carry5 = (s5 + (1 << 20)) >> 21;
    s6 += carry5;
    s5 -= carry5 << 21;
    carry7 = (s7 + (1 << 20)) >> 21;
    s8 += carry7;
    s7 -= carry7 << 21;
    carry9 = (s9 + (1 << 20)) >> 21;
    s10 += carry9;
    s9 -= carry9 << 21;
    carry11 = (s11 + (1 << 20)) >> 21;
    s12 += carry11;
    s11 -= carry11 << 21;
    carry13 = (s13 + (1 << 20)) >> 21;
    s14 += carry13;
    s13 -= carry13 << 21;
    carry15 = (s15 + (1 << 20)) >> 21;
    s16 += carry15;
    s15 -= carry15 << 21;
    carry17 = (s17 + (1 << 20)) >> 21;
    s18 += carry17;
    s17 -= carry17 << 21;
    carry19 = (s19 + (1 << 20)) >> 21;
    s20 += carry19;
    s19 -= carry19 << 21;
    carry21 = (s21 + (1 << 20)) >> 21;
    s22 += carry21;
    s21 -= carry21 << 21;

    s11 += s23 * 666643;
    s12 += s23 * 470296;
    s13 += s23 * 654183;
    s14 -= s23 * 997805;
    s15 += s23 * 136657;
    s16 -= s23 * 683901;
    s23 = 0;

    s10 += s22 * 666643;
    s11 += s22 * 470296;
    s12 += s22 * 654183;
    s13 -= s22 * 997805;
    s14 += s22 * 136657;
    s15 -= s22 * 683901;
    s22 = 0;

    s9 += s21 * 666643;
    s10 += s21 * 470296;
    s11 += s21 * 654183;
    s12 -= s21 * 997805;
    s13 += s21 * 136657;
    s14 -= s21 * 683901;
    s21 = 0;

    s8 += s20 * 666643;
    s9 += s20 * 470296;
    s10 += s20 * 654183;
    s11 -= s20 * 997805;
    s12 += s20 * 136657;
    s13 -= s20 * 683901;
    s20 = 0;

    s7 += s19 * 666643;
    s8 += s19 * 470296;
    s9 += s19 * 654183;
    s10 -= s19 * 997805;
    s11 += s19 * 136657;
    s12 -= s19 * 683901;
    s19 = 0;

    s6 += s18 * 666643;
    s7 += s18 * 470296;
    s8 += s18 * 654183;
    s9 -= s18 * 997805;
    s10 += s18 * 136657;
    s11 -= s18 * 683901;
    s18 = 0;

    carry6 = (s6 + (1 << 20)) >> 21;
    s7 += carry6;
    s6 -= carry6 << 21;
    carry8 = (s8 + (1 << 20)) >> 21;
    s9 += carry8;
    s8 -= carry8 << 21;
    carry10 = (s10 + (1 << 20)) >> 21;
    s11 += carry10;
    s10 -= carry10 << 21;
    carry12 = (s12 + (1 << 20)) >> 21;
    s13 += carry12;
    s12 -= carry12 << 21;
    carry14 = (s14 + (1 << 20)) >> 21;
    s15 += carry14;
    s14 -= carry14 << 21;
    carry16 = (s16 + (1 << 20)) >> 21;
    s17 += carry16;
    s16 -= carry16 << 21;

    carry7 = (s7 + (1 << 20)) >> 21;
    s8 += carry7;
    s7 -= carry7 << 21;
    carry9 = (s9 + (1 << 20)) >> 21;
    s10 += carry9;
    s9 -= carry9 << 21;
    carry11 = (s11 + (1 << 20)) >> 21;
    s12 += carry11;
    s11 -= carry11 << 21;
    carry13 = (s13 + (1 << 20)) >> 21;
    s14 += carry13;
    s13 -= carry13 << 21;
    carry15 = (s15 + (1 << 20)) >> 21;
    s16 += carry15;
    s15 -= carry15 << 21;

    s5 += s17 * 666643;
    s6 += s17 * 470296;
    s7 += s17 * 654183;
    s8 -= s17 * 997805;
    s9 += s17 * 136657;
    s10 -= s17 * 683901;
    s17 = 0;

    s4 += s16 * 666643;
    s5 += s16 * 470296;
    s6 += s16 * 654183;
    s7 -= s16 * 997805;
    s8 += s16 * 136657;
    s9 -= s16 * 683901;
    s16 = 0;

    s3 += s15 * 666643;
    s4 += s15 * 470296;
    s5 += s15 * 654183;
    s6 -= s15 * 997805;
    s7 += s15 * 136657;
    s8 -= s15 * 683901;
    s15 = 0;

    s2 += s14 * 666643;
    s3 += s14 * 470296;
    s4 += s14 * 654183;
    s5 -= s14 * 997805;
    s6 += s14 * 136657;
    s7 -= s14 * 683901;
    s14 = 0;

    s1 += s13 * 666643;
    s2 += s13 * 470296;
    s3 += s13 * 654183;
    s4 -= s13 * 997805;
    s5 += s13 * 136657;
    s6 -= s13 * 683901;
    s13 = 0;

    s0 += s12 * 666643;
    s1 += s12 * 470296;
    s2 += s12 * 654183;
    s3 -= s12 * 997805;
    s4 += s12 * 136657;
    s5 -= s12 * 683901;
    s12 = 0;

    carry0 = (s0 + (1 << 20)) >> 21;
    s1 += carry0;
    s0 -= carry0 << 21;
    carry2 = (s2 + (1 << 20)) >> 21;
    s3 += carry2;
    s2 -= carry2 << 21;
    carry4 = (s4 + (1 << 20)) >> 21;
    s5 += carry4;
    s4 -= carry4 << 21;
    carry6 = (s6 + (1 << 20)) >> 21;
    s7 += carry6;
    s6 -= carry6 << 21;
    carry8 = (s8 + (1 << 20)) >> 21;
    s9 += carry8;
    s8 -= carry8 << 21;
    carry10 = (s10 + (1 << 20)) >> 21;
    s11 += carry10;
    s10 -= carry10 << 21;

    carry1 = (s1 + (1 << 20)) >> 21;
    s2 += carry1;
    s1 -= carry1 << 21;
    carry3 = (s3 + (1 << 20)) >> 21;
    s4 += carry3;
    s3 -= carry3 << 21;
    carry5 = (s5 + (1 << 20)) >> 21;
    s6 += carry5;
    s5 -= carry5 << 21;
    carry7 = (s7 + (1 << 20)) >> 21;
    s8 += carry7;
    s7 -= carry7 << 21;
    carry9 = (s9 + (1 << 20)) >> 21;
    s10 += carry9;
    s9 -= carry9 << 21;
    carry11 = (s11 + (1 << 20)) >> 21;
    s12 += carry11;
    s11 -= carry11 << 21;

    s0 += s12 * 666643;
    s1 += s12 * 470296;
    s2 += s12 * 654183;
    s3 -= s12 * 997805;
    s4 += s12 * 136657;
    s5 -= s12 * 683901;
    s12 = 0;

    carry0 = s0 >> 21;
    s1 += carry0;
    s0 -= carry0 << 21;
    carry1 = s1 >> 21;
    s2 += carry1;
    s1 -= carry1 << 21;
    carry2 = s2 >> 21;
    s3 += carry2;
    s2 -= carry2 << 21;
    carry3 = s3 >> 21;
    s4 += carry3;
    s3 -= carry3 << 21;
    carry4 = s4 >> 21;
    s5 += carry4;
    s4 -= carry4 << 21;
    carry5 = s5 >> 21;
    s6 += carry5;
    s5 -= carry5 << 21;
    carry6 = s6 >> 21;
    s7 += carry6;
    s6 -= carry6 << 21;
    carry7 = s7 >> 21;
    s8 += carry7;
    s7 -= carry7 << 21;
    carry8 = s8 >> 21;
    s9 += carry8;
    s8 -= carry8 << 21;
    carry9 = s9 >> 21;
    s10 += carry9;
    s9 -= carry9 << 21;
    carry10 = s10 >> 21;
    s11 += carry10;
    s10 -= carry10 << 21;
    carry11 = s11 >> 21;
    s12 += carry11;
    s11 -= carry11 << 21;

    s0 += s12 * 666643;
    s1 += s12 * 470296;
    s2 += s12 * 654183;
    s3 -= s12 * 997805;
    s4 += s12 * 136657;
    s5 -= s12 * 683901;
    s12 = 0;

    carry0 = s0 >> 21;
    s1 += carry0;
    s0 -= carry0 << 21;
    carry1 = s1 >> 21;
    s2 += carry1;
    s1 -= carry1 << 21;
    carry2 = s2 >> 21;
    s3 += carry2;
    s2 -= carry2 << 21;
    carry3 = s3 >> 21;
    s4 += carry3;
    s3 -= carry3 << 21;
    carry4 = s4 >> 21;
    s5 += carry4;
    s4 -= carry4 << 21;
    carry5 = s5 >> 21;
    s6 += carry5;
    s5 -= carry5 << 21;
    carry6 = s6 >> 21;
    s7 += carry6;
    s6 -= carry6 << 21;
    carry7 = s7 >> 21;
    s8 += carry7;
    s7 -= carry7 << 21;
    carry8 = s8 >> 21;
    s9 += carry8;
    s8 -= carry8 << 21;
    carry9 = s9 >> 21;
    s10 += carry9;
    s9 -= carry9 << 21;
    carry10 = s10 >> 21;
    s11 += carry10;
    s10 -= carry10 << 21;

    s[0] = s0 >> 0;
    s[1] = s0 >> 8;
    s[2] = (s0 >> 16) | (s1 << 5);
    s[3] = s1 >> 3;
    s[4] = s1 >> 11;
    s[5] = (s1 >> 19) | (s2 << 2);
    s[6] = s2 >> 6;
    s[7] = (s2 >> 14) | (s3 << 7);
    s[8] = s3 >> 1;
    s[9] = s3 >> 9;
    s[10] = (s3 >> 17) | (s4 << 4);
    s[11] = s4 >> 4;
    s[12] = s4 >> 12;
    s[13] = (s4 >> 20) | (s5 << 1);
    s[14] = s5 >> 7;
    s[15] = (s5 >> 15) | (s6 << 6);
    s[16] = s6 >> 2;
    s[17] = s6 >> 10;
    s[18] = (s6 >> 18) | (s7 << 3);
    s[19] = s7 >> 5;
    s[20] = s7 >> 13;
    s[21] = s8 >> 0;
    s[22] = s8 >> 8;
    s[23] = (s8 >> 16) | (s9 << 5);
    s[24] = s9 >> 3;
    s[25] = s9 >> 11;
    s[26] = (s9 >> 19) | (s10 << 2);
    s[27] = s10 >> 6;
    s[28] = (s10 >> 14) | (s11 << 7);
    s[29] = s11 >> 1;
    s[30] = s11 >> 9;
    s[31] = s11 >> 17;
}

void fe_sub(fe h, fe f, fe g)
{
    crypto_int32 f0 = f[0];
    crypto_int32 f1 = f[1];
    crypto_int32 f2 = f[2];
    crypto_int32 f3 = f[3];
    crypto_int32 f4 = f[4];
    crypto_int32 f5 = f[5];
    crypto_int32 f6 = f[6];
    crypto_int32 f7 = f[7];
    crypto_int32 f8 = f[8];
    crypto_int32 f9 = f[9];
    crypto_int32 g0 = g[0];
    crypto_int32 g1 = g[1];
    crypto_int32 g2 = g[2];
    crypto_int32 g3 = g[3];
    crypto_int32 g4 = g[4];
    crypto_int32 g5 = g[5];
    crypto_int32 g6 = g[6];
    crypto_int32 g7 = g[7];
    crypto_int32 g8 = g[8];
    crypto_int32 g9 = g[9];
    crypto_int32 h0 = f0 - g0;
    crypto_int32 h1 = f1 - g1;
    crypto_int32 h2 = f2 - g2;
    crypto_int32 h3 = f3 - g3;
    crypto_int32 h4 = f4 - g4;
    crypto_int32 h5 = f5 - g5;
    crypto_int32 h6 = f6 - g6;
    crypto_int32 h7 = f7 - g7;
    crypto_int32 h8 = f8 - g8;
    crypto_int32 h9 = f9 - g9;
    h[0] = h0;
    h[1] = h1;
    h[2] = h2;
    h[3] = h3;
    h[4] = h4;
    h[5] = h5;
    h[6] = h6;
    h[7] = h7;
    h[8] = h8;
    h[9] = h9;
}

void fe_frombytes(fe h, const unsigned char *s)
{
    crypto_int64 h0 = load_4(s);
    crypto_int64 h1 = load_3(s + 4) << 6;
    crypto_int64 h2 = load_3(s + 7) << 5;
    crypto_int64 h3 = load_3(s + 10) << 3;
    crypto_int64 h4 = load_3(s + 13) << 2;
    crypto_int64 h5 = load_4(s + 16);
    crypto_int64 h6 = load_3(s + 20) << 7;
    crypto_int64 h7 = load_3(s + 23) << 5;
    crypto_int64 h8 = load_3(s + 26) << 4;
    crypto_int64 h9 = (load_3(s + 29) & 8388607) << 2;
    crypto_int64 carry0;
    crypto_int64 carry1;
    crypto_int64 carry2;
    crypto_int64 carry3;
    crypto_int64 carry4;
    crypto_int64 carry5;
    crypto_int64 carry6;
    crypto_int64 carry7;
    crypto_int64 carry8;
    crypto_int64 carry9;

    carry9 = (h9 + (crypto_int64)(1 << 24)) >> 25;
    h0 += carry9 * 19;
    h9 -= carry9 << 25;
    carry1 = (h1 + (crypto_int64)(1 << 24)) >> 25;
    h2 += carry1;
    h1 -= carry1 << 25;
    carry3 = (h3 + (crypto_int64)(1 << 24)) >> 25;
    h4 += carry3;
    h3 -= carry3 << 25;
    carry5 = (h5 + (crypto_int64)(1 << 24)) >> 25;
    h6 += carry5;
    h5 -= carry5 << 25;
    carry7 = (h7 + (crypto_int64)(1 << 24)) >> 25;
    h8 += carry7;
    h7 -= carry7 << 25;

    carry0 = (h0 + (crypto_int64)(1 << 25)) >> 26;
    h1 += carry0;
    h0 -= carry0 << 26;
    carry2 = (h2 + (crypto_int64)(1 << 25)) >> 26;
    h3 += carry2;
    h2 -= carry2 << 26;
    carry4 = (h4 + (crypto_int64)(1 << 25)) >> 26;
    h5 += carry4;
    h4 -= carry4 << 26;
    carry6 = (h6 + (crypto_int64)(1 << 25)) >> 26;
    h7 += carry6;
    h6 -= carry6 << 26;
    carry8 = (h8 + (crypto_int64)(1 << 25)) >> 26;
    h9 += carry8;
    h8 -= carry8 << 26;

    h[0] = h0;
    h[1] = h1;
    h[2] = h2;
    h[3] = h3;
    h[4] = h4;
    h[5] = h5;
    h[6] = h6;
    h[7] = h7;
    h[8] = h8;
    h[9] = h9;
}

void fe_1(fe h)
{
    h[0] = 1;
    h[1] = 0;
    h[2] = 0;
    h[3] = 0;
    h[4] = 0;
    h[5] = 0;
    h[6] = 0;
    h[7] = 0;
    h[8] = 0;
    h[9] = 0;
}

void fe_add(fe h, const fe f, const fe g)
{
    crypto_int32 f0 = f[0];
    crypto_int32 f1 = f[1];
    crypto_int32 f2 = f[2];
    crypto_int32 f3 = f[3];
    crypto_int32 f4 = f[4];
    crypto_int32 f5 = f[5];
    crypto_int32 f6 = f[6];
    crypto_int32 f7 = f[7];
    crypto_int32 f8 = f[8];
    crypto_int32 f9 = f[9];
    crypto_int32 g0 = g[0];
    crypto_int32 g1 = g[1];
    crypto_int32 g2 = g[2];
    crypto_int32 g3 = g[3];
    crypto_int32 g4 = g[4];
    crypto_int32 g5 = g[5];
    crypto_int32 g6 = g[6];
    crypto_int32 g7 = g[7];
    crypto_int32 g8 = g[8];
    crypto_int32 g9 = g[9];
    crypto_int32 h0 = f0 + g0;
    crypto_int32 h1 = f1 + g1;
    crypto_int32 h2 = f2 + g2;
    crypto_int32 h3 = f3 + g3;
    crypto_int32 h4 = f4 + g4;
    crypto_int32 h5 = f5 + g5;
    crypto_int32 h6 = f6 + g6;
    crypto_int32 h7 = f7 + g7;
    crypto_int32 h8 = f8 + g8;
    crypto_int32 h9 = f9 + g9;
    h[0] = h0;
    h[1] = h1;
    h[2] = h2;
    h[3] = h3;
    h[4] = h4;
    h[5] = h5;
    h[6] = h6;
    h[7] = h7;
    h[8] = h8;
    h[9] = h9;
}

void fe_sq(fe h, const fe f)
{
    crypto_int32 f0 = f[0];
    crypto_int32 f1 = f[1];
    crypto_int32 f2 = f[2];
    crypto_int32 f3 = f[3];
    crypto_int32 f4 = f[4];
    crypto_int32 f5 = f[5];
    crypto_int32 f6 = f[6];
    crypto_int32 f7 = f[7];
    crypto_int32 f8 = f[8];
    crypto_int32 f9 = f[9];
    crypto_int32 f0_2 = 2 * f0;
    crypto_int32 f1_2 = 2 * f1;
    crypto_int32 f2_2 = 2 * f2;
    crypto_int32 f3_2 = 2 * f3;
    crypto_int32 f4_2 = 2 * f4;
    crypto_int32 f5_2 = 2 * f5;
    crypto_int32 f6_2 = 2 * f6;
    crypto_int32 f7_2 = 2 * f7;
    crypto_int32 f5_38 = 38 * f5; /* 1.959375*2^30 */
    crypto_int32 f6_19 = 19 * f6; /* 1.959375*2^30 */
    crypto_int32 f7_38 = 38 * f7; /* 1.959375*2^30 */
    crypto_int32 f8_19 = 19 * f8; /* 1.959375*2^30 */
    crypto_int32 f9_38 = 38 * f9; /* 1.959375*2^30 */
    crypto_int64 f0f0 = f0 * (crypto_int64)f0;
    crypto_int64 f0f1_2 = f0_2 * (crypto_int64)f1;
    crypto_int64 f0f2_2 = f0_2 * (crypto_int64)f2;
    crypto_int64 f0f3_2 = f0_2 * (crypto_int64)f3;
    crypto_int64 f0f4_2 = f0_2 * (crypto_int64)f4;
    crypto_int64 f0f5_2 = f0_2 * (crypto_int64)f5;
    crypto_int64 f0f6_2 = f0_2 * (crypto_int64)f6;
    crypto_int64 f0f7_2 = f0_2 * (crypto_int64)f7;
    crypto_int64 f0f8_2 = f0_2 * (crypto_int64)f8;
    crypto_int64 f0f9_2 = f0_2 * (crypto_int64)f9;
    crypto_int64 f1f1_2 = f1_2 * (crypto_int64)f1;
    crypto_int64 f1f2_2 = f1_2 * (crypto_int64)f2;
    crypto_int64 f1f3_4 = f1_2 * (crypto_int64)f3_2;
    crypto_int64 f1f4_2 = f1_2 * (crypto_int64)f4;
    crypto_int64 f1f5_4 = f1_2 * (crypto_int64)f5_2;
    crypto_int64 f1f6_2 = f1_2 * (crypto_int64)f6;
    crypto_int64 f1f7_4 = f1_2 * (crypto_int64)f7_2;
    crypto_int64 f1f8_2 = f1_2 * (crypto_int64)f8;
    crypto_int64 f1f9_76 = f1_2 * (crypto_int64)f9_38;
    crypto_int64 f2f2 = f2 * (crypto_int64)f2;
    crypto_int64 f2f3_2 = f2_2 * (crypto_int64)f3;
    crypto_int64 f2f4_2 = f2_2 * (crypto_int64)f4;
    crypto_int64 f2f5_2 = f2_2 * (crypto_int64)f5;
    crypto_int64 f2f6_2 = f2_2 * (crypto_int64)f6;
    crypto_int64 f2f7_2 = f2_2 * (crypto_int64)f7;
    crypto_int64 f2f8_38 = f2_2 * (crypto_int64)f8_19;
    crypto_int64 f2f9_38 = f2 * (crypto_int64)f9_38;
    crypto_int64 f3f3_2 = f3_2 * (crypto_int64)f3;
    crypto_int64 f3f4_2 = f3_2 * (crypto_int64)f4;
    crypto_int64 f3f5_4 = f3_2 * (crypto_int64)f5_2;
    crypto_int64 f3f6_2 = f3_2 * (crypto_int64)f6;
    crypto_int64 f3f7_76 = f3_2 * (crypto_int64)f7_38;
    crypto_int64 f3f8_38 = f3_2 * (crypto_int64)f8_19;
    crypto_int64 f3f9_76 = f3_2 * (crypto_int64)f9_38;
    crypto_int64 f4f4 = f4 * (crypto_int64)f4;
    crypto_int64 f4f5_2 = f4_2 * (crypto_int64)f5;
    crypto_int64 f4f6_38 = f4_2 * (crypto_int64)f6_19;
    crypto_int64 f4f7_38 = f4 * (crypto_int64)f7_38;
    crypto_int64 f4f8_38 = f4_2 * (crypto_int64)f8_19;
    crypto_int64 f4f9_38 = f4 * (crypto_int64)f9_38;
    crypto_int64 f5f5_38 = f5 * (crypto_int64)f5_38;
    crypto_int64 f5f6_38 = f5_2 * (crypto_int64)f6_19;
    crypto_int64 f5f7_76 = f5_2 * (crypto_int64)f7_38;
    crypto_int64 f5f8_38 = f5_2 * (crypto_int64)f8_19;
    crypto_int64 f5f9_76 = f5_2 * (crypto_int64)f9_38;
    crypto_int64 f6f6_19 = f6 * (crypto_int64)f6_19;
    crypto_int64 f6f7_38 = f6 * (crypto_int64)f7_38;
    crypto_int64 f6f8_38 = f6_2 * (crypto_int64)f8_19;
    crypto_int64 f6f9_38 = f6 * (crypto_int64)f9_38;
    crypto_int64 f7f7_38 = f7 * (crypto_int64)f7_38;
    crypto_int64 f7f8_38 = f7_2 * (crypto_int64)f8_19;
    crypto_int64 f7f9_76 = f7_2 * (crypto_int64)f9_38;
    crypto_int64 f8f8_19 = f8 * (crypto_int64)f8_19;
    crypto_int64 f8f9_38 = f8 * (crypto_int64)f9_38;
    crypto_int64 f9f9_38 = f9 * (crypto_int64)f9_38;
    crypto_int64 h0 = f0f0 + f1f9_76 + f2f8_38 + f3f7_76 + f4f6_38 + f5f5_38;
    crypto_int64 h1 = f0f1_2 + f2f9_38 + f3f8_38 + f4f7_38 + f5f6_38;
    crypto_int64 h2 = f0f2_2 + f1f1_2 + f3f9_76 + f4f8_38 + f5f7_76 + f6f6_19;
    crypto_int64 h3 = f0f3_2 + f1f2_2 + f4f9_38 + f5f8_38 + f6f7_38;
    crypto_int64 h4 = f0f4_2 + f1f3_4 + f2f2 + f5f9_76 + f6f8_38 + f7f7_38;
    crypto_int64 h5 = f0f5_2 + f1f4_2 + f2f3_2 + f6f9_38 + f7f8_38;
    crypto_int64 h6 = f0f6_2 + f1f5_4 + f2f4_2 + f3f3_2 + f7f9_76 + f8f8_19;
    crypto_int64 h7 = f0f7_2 + f1f6_2 + f2f5_2 + f3f4_2 + f8f9_38;
    crypto_int64 h8 = f0f8_2 + f1f7_4 + f2f6_2 + f3f5_4 + f4f4 + f9f9_38;
    crypto_int64 h9 = f0f9_2 + f1f8_2 + f2f7_2 + f3f6_2 + f4f5_2;
    crypto_int64 carry0;
    crypto_int64 carry1;
    crypto_int64 carry2;
    crypto_int64 carry3;
    crypto_int64 carry4;
    crypto_int64 carry5;
    crypto_int64 carry6;
    crypto_int64 carry7;
    crypto_int64 carry8;
    crypto_int64 carry9;

    carry0 = (h0 + (crypto_int64)(1 << 25)) >> 26;
    h1 += carry0;
    h0 -= carry0 << 26;
    carry4 = (h4 + (crypto_int64)(1 << 25)) >> 26;
    h5 += carry4;
    h4 -= carry4 << 26;

    carry1 = (h1 + (crypto_int64)(1 << 24)) >> 25;
    h2 += carry1;
    h1 -= carry1 << 25;
    carry5 = (h5 + (crypto_int64)(1 << 24)) >> 25;
    h6 += carry5;
    h5 -= carry5 << 25;

    carry2 = (h2 + (crypto_int64)(1 << 25)) >> 26;
    h3 += carry2;
    h2 -= carry2 << 26;
    carry6 = (h6 + (crypto_int64)(1 << 25)) >> 26;
    h7 += carry6;
    h6 -= carry6 << 26;

    carry3 = (h3 + (crypto_int64)(1 << 24)) >> 25;
    h4 += carry3;
    h3 -= carry3 << 25;
    carry7 = (h7 + (crypto_int64)(1 << 24)) >> 25;
    h8 += carry7;
    h7 -= carry7 << 25;

    carry4 = (h4 + (crypto_int64)(1 << 25)) >> 26;
    h5 += carry4;
    h4 -= carry4 << 26;
    carry8 = (h8 + (crypto_int64)(1 << 25)) >> 26;
    h9 += carry8;
    h8 -= carry8 << 26;

    carry9 = (h9 + (crypto_int64)(1 << 24)) >> 25;
    h0 += carry9 * 19;
    h9 -= carry9 << 25;

    carry0 = (h0 + (crypto_int64)(1 << 25)) >> 26;
    h1 += carry0;
    h0 -= carry0 << 26;

    h[0] = h0;
    h[1] = h1;
    h[2] = h2;
    h[3] = h3;
    h[4] = h4;
    h[5] = h5;
    h[6] = h6;
    h[7] = h7;
    h[8] = h8;
    h[9] = h9;
}

void fe_mul(fe h, const fe f, const fe g)
{
    crypto_int32 f0 = f[0];
    crypto_int32 f1 = f[1];
    crypto_int32 f2 = f[2];
    crypto_int32 f3 = f[3];
    crypto_int32 f4 = f[4];
    crypto_int32 f5 = f[5];
    crypto_int32 f6 = f[6];
    crypto_int32 f7 = f[7];
    crypto_int32 f8 = f[8];
    crypto_int32 f9 = f[9];
    crypto_int32 g0 = g[0];
    crypto_int32 g1 = g[1];
    crypto_int32 g2 = g[2];
    crypto_int32 g3 = g[3];
    crypto_int32 g4 = g[4];
    crypto_int32 g5 = g[5];
    crypto_int32 g6 = g[6];
    crypto_int32 g7 = g[7];
    crypto_int32 g8 = g[8];
    crypto_int32 g9 = g[9];
    crypto_int32 g1_19 = 19 * g1; /* 1.959375*2^29 */
    crypto_int32 g2_19 = 19 * g2; /* 1.959375*2^30; still ok */
    crypto_int32 g3_19 = 19 * g3;
    crypto_int32 g4_19 = 19 * g4;
    crypto_int32 g5_19 = 19 * g5;
    crypto_int32 g6_19 = 19 * g6;
    crypto_int32 g7_19 = 19 * g7;
    crypto_int32 g8_19 = 19 * g8;
    crypto_int32 g9_19 = 19 * g9;
    crypto_int32 f1_2 = 2 * f1;
    crypto_int32 f3_2 = 2 * f3;
    crypto_int32 f5_2 = 2 * f5;
    crypto_int32 f7_2 = 2 * f7;
    crypto_int32 f9_2 = 2 * f9;
    crypto_int64 f0g0 = f0 * (crypto_int64)g0;
    crypto_int64 f0g1 = f0 * (crypto_int64)g1;
    crypto_int64 f0g2 = f0 * (crypto_int64)g2;
    crypto_int64 f0g3 = f0 * (crypto_int64)g3;
    crypto_int64 f0g4 = f0 * (crypto_int64)g4;
    crypto_int64 f0g5 = f0 * (crypto_int64)g5;
    crypto_int64 f0g6 = f0 * (crypto_int64)g6;
    crypto_int64 f0g7 = f0 * (crypto_int64)g7;
    crypto_int64 f0g8 = f0 * (crypto_int64)g8;
    crypto_int64 f0g9 = f0 * (crypto_int64)g9;
    crypto_int64 f1g0 = f1 * (crypto_int64)g0;
    crypto_int64 f1g1_2 = f1_2 * (crypto_int64)g1;
    crypto_int64 f1g2 = f1 * (crypto_int64)g2;
    crypto_int64 f1g3_2 = f1_2 * (crypto_int64)g3;
    crypto_int64 f1g4 = f1 * (crypto_int64)g4;
    crypto_int64 f1g5_2 = f1_2 * (crypto_int64)g5;
    crypto_int64 f1g6 = f1 * (crypto_int64)g6;
    crypto_int64 f1g7_2 = f1_2 * (crypto_int64)g7;
    crypto_int64 f1g8 = f1 * (crypto_int64)g8;
    crypto_int64 f1g9_38 = f1_2 * (crypto_int64)g9_19;
    crypto_int64 f2g0 = f2 * (crypto_int64)g0;
    crypto_int64 f2g1 = f2 * (crypto_int64)g1;
    crypto_int64 f2g2 = f2 * (crypto_int64)g2;
    crypto_int64 f2g3 = f2 * (crypto_int64)g3;
    crypto_int64 f2g4 = f2 * (crypto_int64)g4;
    crypto_int64 f2g5 = f2 * (crypto_int64)g5;
    crypto_int64 f2g6 = f2 * (crypto_int64)g6;
    crypto_int64 f2g7 = f2 * (crypto_int64)g7;
    crypto_int64 f2g8_19 = f2 * (crypto_int64)g8_19;
    crypto_int64 f2g9_19 = f2 * (crypto_int64)g9_19;
    crypto_int64 f3g0 = f3 * (crypto_int64)g0;
    crypto_int64 f3g1_2 = f3_2 * (crypto_int64)g1;
    crypto_int64 f3g2 = f3 * (crypto_int64)g2;
    crypto_int64 f3g3_2 = f3_2 * (crypto_int64)g3;
    crypto_int64 f3g4 = f3 * (crypto_int64)g4;
    crypto_int64 f3g5_2 = f3_2 * (crypto_int64)g5;
    crypto_int64 f3g6 = f3 * (crypto_int64)g6;
    crypto_int64 f3g7_38 = f3_2 * (crypto_int64)g7_19;
    crypto_int64 f3g8_19 = f3 * (crypto_int64)g8_19;
    crypto_int64 f3g9_38 = f3_2 * (crypto_int64)g9_19;
    crypto_int64 f4g0 = f4 * (crypto_int64)g0;
    crypto_int64 f4g1 = f4 * (crypto_int64)g1;
    crypto_int64 f4g2 = f4 * (crypto_int64)g2;
    crypto_int64 f4g3 = f4 * (crypto_int64)g3;
    crypto_int64 f4g4 = f4 * (crypto_int64)g4;
    crypto_int64 f4g5 = f4 * (crypto_int64)g5;
    crypto_int64 f4g6_19 = f4 * (crypto_int64)g6_19;
    crypto_int64 f4g7_19 = f4 * (crypto_int64)g7_19;
    crypto_int64 f4g8_19 = f4 * (crypto_int64)g8_19;
    crypto_int64 f4g9_19 = f4 * (crypto_int64)g9_19;
    crypto_int64 f5g0 = f5 * (crypto_int64)g0;
    crypto_int64 f5g1_2 = f5_2 * (crypto_int64)g1;
    crypto_int64 f5g2 = f5 * (crypto_int64)g2;
    crypto_int64 f5g3_2 = f5_2 * (crypto_int64)g3;
    crypto_int64 f5g4 = f5 * (crypto_int64)g4;
    crypto_int64 f5g5_38 = f5_2 * (crypto_int64)g5_19;
    crypto_int64 f5g6_19 = f5 * (crypto_int64)g6_19;
    crypto_int64 f5g7_38 = f5_2 * (crypto_int64)g7_19;
    crypto_int64 f5g8_19 = f5 * (crypto_int64)g8_19;
    crypto_int64 f5g9_38 = f5_2 * (crypto_int64)g9_19;
    crypto_int64 f6g0 = f6 * (crypto_int64)g0;
    crypto_int64 f6g1 = f6 * (crypto_int64)g1;
    crypto_int64 f6g2 = f6 * (crypto_int64)g2;
    crypto_int64 f6g3 = f6 * (crypto_int64)g3;
    crypto_int64 f6g4_19 = f6 * (crypto_int64)g4_19;
    crypto_int64 f6g5_19 = f6 * (crypto_int64)g5_19;
    crypto_int64 f6g6_19 = f6 * (crypto_int64)g6_19;
    crypto_int64 f6g7_19 = f6 * (crypto_int64)g7_19;
    crypto_int64 f6g8_19 = f6 * (crypto_int64)g8_19;
    crypto_int64 f6g9_19 = f6 * (crypto_int64)g9_19;
    crypto_int64 f7g0 = f7 * (crypto_int64)g0;
    crypto_int64 f7g1_2 = f7_2 * (crypto_int64)g1;
    crypto_int64 f7g2 = f7 * (crypto_int64)g2;
    crypto_int64 f7g3_38 = f7_2 * (crypto_int64)g3_19;
    crypto_int64 f7g4_19 = f7 * (crypto_int64)g4_19;
    crypto_int64 f7g5_38 = f7_2 * (crypto_int64)g5_19;
    crypto_int64 f7g6_19 = f7 * (crypto_int64)g6_19;
    crypto_int64 f7g7_38 = f7_2 * (crypto_int64)g7_19;
    crypto_int64 f7g8_19 = f7 * (crypto_int64)g8_19;
    crypto_int64 f7g9_38 = f7_2 * (crypto_int64)g9_19;
    crypto_int64 f8g0 = f8 * (crypto_int64)g0;
    crypto_int64 f8g1 = f8 * (crypto_int64)g1;
    crypto_int64 f8g2_19 = f8 * (crypto_int64)g2_19;
    crypto_int64 f8g3_19 = f8 * (crypto_int64)g3_19;
    crypto_int64 f8g4_19 = f8 * (crypto_int64)g4_19;
    crypto_int64 f8g5_19 = f8 * (crypto_int64)g5_19;
    crypto_int64 f8g6_19 = f8 * (crypto_int64)g6_19;
    crypto_int64 f8g7_19 = f8 * (crypto_int64)g7_19;
    crypto_int64 f8g8_19 = f8 * (crypto_int64)g8_19;
    crypto_int64 f8g9_19 = f8 * (crypto_int64)g9_19;
    crypto_int64 f9g0 = f9 * (crypto_int64)g0;
    crypto_int64 f9g1_38 = f9_2 * (crypto_int64)g1_19;
    crypto_int64 f9g2_19 = f9 * (crypto_int64)g2_19;
    crypto_int64 f9g3_38 = f9_2 * (crypto_int64)g3_19;
    crypto_int64 f9g4_19 = f9 * (crypto_int64)g4_19;
    crypto_int64 f9g5_38 = f9_2 * (crypto_int64)g5_19;
    crypto_int64 f9g6_19 = f9 * (crypto_int64)g6_19;
    crypto_int64 f9g7_38 = f9_2 * (crypto_int64)g7_19;
    crypto_int64 f9g8_19 = f9 * (crypto_int64)g8_19;
    crypto_int64 f9g9_38 = f9_2 * (crypto_int64)g9_19;
    crypto_int64 h0 = f0g0 + f1g9_38 + f2g8_19 + f3g7_38 + f4g6_19 + f5g5_38 + f6g4_19 + f7g3_38 + f8g2_19 + f9g1_38;
    crypto_int64 h1 = f0g1 + f1g0 + f2g9_19 + f3g8_19 + f4g7_19 + f5g6_19 + f6g5_19 + f7g4_19 + f8g3_19 + f9g2_19;
    crypto_int64 h2 = f0g2 + f1g1_2 + f2g0 + f3g9_38 + f4g8_19 + f5g7_38 + f6g6_19 + f7g5_38 + f8g4_19 + f9g3_38;
    crypto_int64 h3 = f0g3 + f1g2 + f2g1 + f3g0 + f4g9_19 + f5g8_19 + f6g7_19 + f7g6_19 + f8g5_19 + f9g4_19;
    crypto_int64 h4 = f0g4 + f1g3_2 + f2g2 + f3g1_2 + f4g0 + f5g9_38 + f6g8_19 + f7g7_38 + f8g6_19 + f9g5_38;
    crypto_int64 h5 = f0g5 + f1g4 + f2g3 + f3g2 + f4g1 + f5g0 + f6g9_19 + f7g8_19 + f8g7_19 + f9g6_19;
    crypto_int64 h6 = f0g6 + f1g5_2 + f2g4 + f3g3_2 + f4g2 + f5g1_2 + f6g0 + f7g9_38 + f8g8_19 + f9g7_38;
    crypto_int64 h7 = f0g7 + f1g6 + f2g5 + f3g4 + f4g3 + f5g2 + f6g1 + f7g0 + f8g9_19 + f9g8_19;
    crypto_int64 h8 = f0g8 + f1g7_2 + f2g6 + f3g5_2 + f4g4 + f5g3_2 + f6g2 + f7g1_2 + f8g0 + f9g9_38;
    crypto_int64 h9 = f0g9 + f1g8 + f2g7 + f3g6 + f4g5 + f5g4 + f6g3 + f7g2 + f8g1 + f9g0;
    crypto_int64 carry0;
    crypto_int64 carry1;
    crypto_int64 carry2;
    crypto_int64 carry3;
    crypto_int64 carry4;
    crypto_int64 carry5;
    crypto_int64 carry6;
    crypto_int64 carry7;
    crypto_int64 carry8;
    crypto_int64 carry9;

    /*
    |h0| <= (1.65*1.65*2^52*(1+19+19+19+19)+1.65*1.65*2^50*(38+38+38+38+38))
      i.e. |h0| <= 1.4*2^60; narrower ranges for h2, h4, h6, h8
    |h1| <= (1.65*1.65*2^51*(1+1+19+19+19+19+19+19+19+19))
      i.e. |h1| <= 1.7*2^59; narrower ranges for h3, h5, h7, h9
    */

    carry0 = (h0 + (crypto_int64)(1 << 25)) >> 26;
    h1 += carry0;
    h0 -= carry0 << 26;
    carry4 = (h4 + (crypto_int64)(1 << 25)) >> 26;
    h5 += carry4;
    h4 -= carry4 << 26;
    /* |h0| <= 2^25 */
    /* |h4| <= 2^25 */
    /* |h1| <= 1.71*2^59 */
    /* |h5| <= 1.71*2^59 */

    carry1 = (h1 + (crypto_int64)(1 << 24)) >> 25;
    h2 += carry1;
    h1 -= carry1 << 25;
    carry5 = (h5 + (crypto_int64)(1 << 24)) >> 25;
    h6 += carry5;
    h5 -= carry5 << 25;
    /* |h1| <= 2^24; from now on fits into int32 */
    /* |h5| <= 2^24; from now on fits into int32 */
    /* |h2| <= 1.41*2^60 */
    /* |h6| <= 1.41*2^60 */

    carry2 = (h2 + (crypto_int64)(1 << 25)) >> 26;
    h3 += carry2;
    h2 -= carry2 << 26;
    carry6 = (h6 + (crypto_int64)(1 << 25)) >> 26;
    h7 += carry6;
    h6 -= carry6 << 26;
    /* |h2| <= 2^25; from now on fits into int32 unchanged */
    /* |h6| <= 2^25; from now on fits into int32 unchanged */
    /* |h3| <= 1.71*2^59 */
    /* |h7| <= 1.71*2^59 */

    carry3 = (h3 + (crypto_int64)(1 << 24)) >> 25;
    h4 += carry3;
    h3 -= carry3 << 25;
    carry7 = (h7 + (crypto_int64)(1 << 24)) >> 25;
    h8 += carry7;
    h7 -= carry7 << 25;
    /* |h3| <= 2^24; from now on fits into int32 unchanged */
    /* |h7| <= 2^24; from now on fits into int32 unchanged */
    /* |h4| <= 1.72*2^34 */
    /* |h8| <= 1.41*2^60 */

    carry4 = (h4 + (crypto_int64)(1 << 25)) >> 26;
    h5 += carry4;
    h4 -= carry4 << 26;
    carry8 = (h8 + (crypto_int64)(1 << 25)) >> 26;
    h9 += carry8;
    h8 -= carry8 << 26;
    /* |h4| <= 2^25; from now on fits into int32 unchanged */
    /* |h8| <= 2^25; from now on fits into int32 unchanged */
    /* |h5| <= 1.01*2^24 */
    /* |h9| <= 1.71*2^59 */

    carry9 = (h9 + (crypto_int64)(1 << 24)) >> 25;
    h0 += carry9 * 19;
    h9 -= carry9 << 25;
    /* |h9| <= 2^24; from now on fits into int32 unchanged */
    /* |h0| <= 1.1*2^39 */

    carry0 = (h0 + (crypto_int64)(1 << 25)) >> 26;
    h1 += carry0;
    h0 -= carry0 << 26;
    /* |h0| <= 2^25; from now on fits into int32 unchanged */
    /* |h1| <= 1.01*2^24 */

    h[0] = h0;
    h[1] = h1;
    h[2] = h2;
    h[3] = h3;
    h[4] = h4;
    h[5] = h5;
    h[6] = h6;
    h[7] = h7;
    h[8] = h8;
    h[9] = h9;
}

void fe_invert(fe out, const fe z)
{
    fe t0;
    fe t1;
    fe t2;
    fe t3;
    int i;

    /* qhasm: z2 = z1^2^1 */
    /* asm 1: fe_sq(>z2=fe#1,<z1=fe#11); for (i = 1;i < 1;++i) fe_sq(>z2=fe#1,>z2=fe#1); */
    /* asm 2: fe_sq(>z2=t0,<z1=z); for (i = 1;i < 1;++i) fe_sq(>z2=t0,>z2=t0); */
    fe_sq(t0, z);
    for (i = 1; i < 1; ++i)
        fe_sq(t0, t0);

    /* qhasm: z8 = z2^2^2 */
    /* asm 1: fe_sq(>z8=fe#2,<z2=fe#1); for (i = 1;i < 2;++i) fe_sq(>z8=fe#2,>z8=fe#2); */
    /* asm 2: fe_sq(>z8=t1,<z2=t0); for (i = 1;i < 2;++i) fe_sq(>z8=t1,>z8=t1); */
    fe_sq(t1, t0);
    for (i = 1; i < 2; ++i)
        fe_sq(t1, t1);

    /* qhasm: z9 = z1*z8 */
    /* asm 1: fe_mul(>z9=fe#2,<z1=fe#11,<z8=fe#2); */
    /* asm 2: fe_mul(>z9=t1,<z1=z,<z8=t1); */
    fe_mul(t1, z, t1);

    /* qhasm: z11 = z2*z9 */
    /* asm 1: fe_mul(>z11=fe#1,<z2=fe#1,<z9=fe#2); */
    /* asm 2: fe_mul(>z11=t0,<z2=t0,<z9=t1); */
    fe_mul(t0, t0, t1);

    /* qhasm: z22 = z11^2^1 */
    /* asm 1: fe_sq(>z22=fe#3,<z11=fe#1); for (i = 1;i < 1;++i) fe_sq(>z22=fe#3,>z22=fe#3); */
    /* asm 2: fe_sq(>z22=t2,<z11=t0); for (i = 1;i < 1;++i) fe_sq(>z22=t2,>z22=t2); */
    fe_sq(t2, t0);
    for (i = 1; i < 1; ++i)
        fe_sq(t2, t2);

    /* qhasm: z_5_0 = z9*z22 */
    /* asm 1: fe_mul(>z_5_0=fe#2,<z9=fe#2,<z22=fe#3); */
    /* asm 2: fe_mul(>z_5_0=t1,<z9=t1,<z22=t2); */
    fe_mul(t1, t1, t2);

    /* qhasm: z_10_5 = z_5_0^2^5 */
    /* asm 1: fe_sq(>z_10_5=fe#3,<z_5_0=fe#2); for (i = 1;i < 5;++i) fe_sq(>z_10_5=fe#3,>z_10_5=fe#3); */
    /* asm 2: fe_sq(>z_10_5=t2,<z_5_0=t1); for (i = 1;i < 5;++i) fe_sq(>z_10_5=t2,>z_10_5=t2); */
    fe_sq(t2, t1);
    for (i = 1; i < 5; ++i)
        fe_sq(t2, t2);

    /* qhasm: z_10_0 = z_10_5*z_5_0 */
    /* asm 1: fe_mul(>z_10_0=fe#2,<z_10_5=fe#3,<z_5_0=fe#2); */
    /* asm 2: fe_mul(>z_10_0=t1,<z_10_5=t2,<z_5_0=t1); */
    fe_mul(t1, t2, t1);

    /* qhasm: z_20_10 = z_10_0^2^10 */
    /* asm 1: fe_sq(>z_20_10=fe#3,<z_10_0=fe#2); for (i = 1;i < 10;++i) fe_sq(>z_20_10=fe#3,>z_20_10=fe#3); */
    /* asm 2: fe_sq(>z_20_10=t2,<z_10_0=t1); for (i = 1;i < 10;++i) fe_sq(>z_20_10=t2,>z_20_10=t2); */
    fe_sq(t2, t1);
    for (i = 1; i < 10; ++i)
        fe_sq(t2, t2);

    /* qhasm: z_20_0 = z_20_10*z_10_0 */
    /* asm 1: fe_mul(>z_20_0=fe#3,<z_20_10=fe#3,<z_10_0=fe#2); */
    /* asm 2: fe_mul(>z_20_0=t2,<z_20_10=t2,<z_10_0=t1); */
    fe_mul(t2, t2, t1);

    /* qhasm: z_40_20 = z_20_0^2^20 */
    /* asm 1: fe_sq(>z_40_20=fe#4,<z_20_0=fe#3); for (i = 1;i < 20;++i) fe_sq(>z_40_20=fe#4,>z_40_20=fe#4); */
    /* asm 2: fe_sq(>z_40_20=t3,<z_20_0=t2); for (i = 1;i < 20;++i) fe_sq(>z_40_20=t3,>z_40_20=t3); */
    fe_sq(t3, t2);
    for (i = 1; i < 20; ++i)
        fe_sq(t3, t3);

    /* qhasm: z_40_0 = z_40_20*z_20_0 */
    /* asm 1: fe_mul(>z_40_0=fe#3,<z_40_20=fe#4,<z_20_0=fe#3); */
    /* asm 2: fe_mul(>z_40_0=t2,<z_40_20=t3,<z_20_0=t2); */
    fe_mul(t2, t3, t2);

    /* qhasm: z_50_10 = z_40_0^2^10 */
    /* asm 1: fe_sq(>z_50_10=fe#3,<z_40_0=fe#3); for (i = 1;i < 10;++i) fe_sq(>z_50_10=fe#3,>z_50_10=fe#3); */
    /* asm 2: fe_sq(>z_50_10=t2,<z_40_0=t2); for (i = 1;i < 10;++i) fe_sq(>z_50_10=t2,>z_50_10=t2); */
    fe_sq(t2, t2);
    for (i = 1; i < 10; ++i)
        fe_sq(t2, t2);

    /* qhasm: z_50_0 = z_50_10*z_10_0 */
    /* asm 1: fe_mul(>z_50_0=fe#2,<z_50_10=fe#3,<z_10_0=fe#2); */
    /* asm 2: fe_mul(>z_50_0=t1,<z_50_10=t2,<z_10_0=t1); */
    fe_mul(t1, t2, t1);

    /* qhasm: z_100_50 = z_50_0^2^50 */
    /* asm 1: fe_sq(>z_100_50=fe#3,<z_50_0=fe#2); for (i = 1;i < 50;++i) fe_sq(>z_100_50=fe#3,>z_100_50=fe#3); */
    /* asm 2: fe_sq(>z_100_50=t2,<z_50_0=t1); for (i = 1;i < 50;++i) fe_sq(>z_100_50=t2,>z_100_50=t2); */
    fe_sq(t2, t1);
    for (i = 1; i < 50; ++i)
        fe_sq(t2, t2);

    /* qhasm: z_100_0 = z_100_50*z_50_0 */
    /* asm 1: fe_mul(>z_100_0=fe#3,<z_100_50=fe#3,<z_50_0=fe#2); */
    /* asm 2: fe_mul(>z_100_0=t2,<z_100_50=t2,<z_50_0=t1); */
    fe_mul(t2, t2, t1);

    /* qhasm: z_200_100 = z_100_0^2^100 */
    /* asm 1: fe_sq(>z_200_100=fe#4,<z_100_0=fe#3); for (i = 1;i < 100;++i) fe_sq(>z_200_100=fe#4,>z_200_100=fe#4); */
    /* asm 2: fe_sq(>z_200_100=t3,<z_100_0=t2); for (i = 1;i < 100;++i) fe_sq(>z_200_100=t3,>z_200_100=t3); */
    fe_sq(t3, t2);
    for (i = 1; i < 100; ++i)
        fe_sq(t3, t3);

    /* qhasm: z_200_0 = z_200_100*z_100_0 */
    /* asm 1: fe_mul(>z_200_0=fe#3,<z_200_100=fe#4,<z_100_0=fe#3); */
    /* asm 2: fe_mul(>z_200_0=t2,<z_200_100=t3,<z_100_0=t2); */
    fe_mul(t2, t3, t2);

    /* qhasm: z_250_50 = z_200_0^2^50 */
    /* asm 1: fe_sq(>z_250_50=fe#3,<z_200_0=fe#3); for (i = 1;i < 50;++i) fe_sq(>z_250_50=fe#3,>z_250_50=fe#3); */
    /* asm 2: fe_sq(>z_250_50=t2,<z_200_0=t2); for (i = 1;i < 50;++i) fe_sq(>z_250_50=t2,>z_250_50=t2); */
    fe_sq(t2, t2);
    for (i = 1; i < 50; ++i)
        fe_sq(t2, t2);

    /* qhasm: z_250_0 = z_250_50*z_50_0 */
    /* asm 1: fe_mul(>z_250_0=fe#2,<z_250_50=fe#3,<z_50_0=fe#2); */
    /* asm 2: fe_mul(>z_250_0=t1,<z_250_50=t2,<z_50_0=t1); */
    fe_mul(t1, t2, t1);

    /* qhasm: z_255_5 = z_250_0^2^5 */
    /* asm 1: fe_sq(>z_255_5=fe#2,<z_250_0=fe#2); for (i = 1;i < 5;++i) fe_sq(>z_255_5=fe#2,>z_255_5=fe#2); */
    /* asm 2: fe_sq(>z_255_5=t1,<z_250_0=t1); for (i = 1;i < 5;++i) fe_sq(>z_255_5=t1,>z_255_5=t1); */
    fe_sq(t1, t1);
    for (i = 1; i < 5; ++i)
        fe_sq(t1, t1);

    /* qhasm: z_255_21 = z_255_5*z11 */
    /* asm 1: fe_mul(>z_255_21=fe#12,<z_255_5=fe#2,<z11=fe#1); */
    /* asm 2: fe_mul(>z_255_21=out,<z_255_5=t1,<z11=t0); */
    fe_mul(out, t1, t0);

    /* qhasm: return */

    return;
}

void fe_tobytes(unsigned char *s, const fe h)
{
    crypto_int32 h0 = h[0];
    crypto_int32 h1 = h[1];
    crypto_int32 h2 = h[2];
    crypto_int32 h3 = h[3];
    crypto_int32 h4 = h[4];
    crypto_int32 h5 = h[5];
    crypto_int32 h6 = h[6];
    crypto_int32 h7 = h[7];
    crypto_int32 h8 = h[8];
    crypto_int32 h9 = h[9];
    crypto_int32 q;
    crypto_int32 carry0;
    crypto_int32 carry1;
    crypto_int32 carry2;
    crypto_int32 carry3;
    crypto_int32 carry4;
    crypto_int32 carry5;
    crypto_int32 carry6;
    crypto_int32 carry7;
    crypto_int32 carry8;
    crypto_int32 carry9;

    q = (19 * h9 + (((crypto_int32)1) << 24)) >> 25;
    q = (h0 + q) >> 26;
    q = (h1 + q) >> 25;
    q = (h2 + q) >> 26;
    q = (h3 + q) >> 25;
    q = (h4 + q) >> 26;
    q = (h5 + q) >> 25;
    q = (h6 + q) >> 26;
    q = (h7 + q) >> 25;
    q = (h8 + q) >> 26;
    q = (h9 + q) >> 25;

    /* Goal: Output h-(2^255-19)q, which is between 0 and 2^255-20. */
    h0 += 19 * q;
    /* Goal: Output h-2^255 q, which is between 0 and 2^255-20. */

    carry0 = h0 >> 26;
    h1 += carry0;
    h0 -= carry0 << 26;
    carry1 = h1 >> 25;
    h2 += carry1;
    h1 -= carry1 << 25;
    carry2 = h2 >> 26;
    h3 += carry2;
    h2 -= carry2 << 26;
    carry3 = h3 >> 25;
    h4 += carry3;
    h3 -= carry3 << 25;
    carry4 = h4 >> 26;
    h5 += carry4;
    h4 -= carry4 << 26;
    carry5 = h5 >> 25;
    h6 += carry5;
    h5 -= carry5 << 25;
    carry6 = h6 >> 26;
    h7 += carry6;
    h6 -= carry6 << 26;
    carry7 = h7 >> 25;
    h8 += carry7;
    h7 -= carry7 << 25;
    carry8 = h8 >> 26;
    h9 += carry8;
    h8 -= carry8 << 26;
    carry9 = h9 >> 25;
    h9 -= carry9 << 25;
    /* h10 = carry9 */

    /*
    Goal: Output h0+...+2^255 h10-2^255 q, which is between 0 and 2^255-20.
    Have h0+...+2^230 h9 between 0 and 2^255-1;
    evidently 2^255 h10-2^255 q = 0.
    Goal: Output h0+...+2^230 h9.
    */

    s[0] = h0 >> 0;
    s[1] = h0 >> 8;
    s[2] = h0 >> 16;
    s[3] = (h0 >> 24) | (h1 << 2);
    s[4] = h1 >> 6;
    s[5] = h1 >> 14;
    s[6] = (h1 >> 22) | (h2 << 3);
    s[7] = h2 >> 5;
    s[8] = h2 >> 13;
    s[9] = (h2 >> 21) | (h3 << 5);
    s[10] = h3 >> 3;
    s[11] = h3 >> 11;
    s[12] = (h3 >> 19) | (h4 << 6);
    s[13] = h4 >> 2;
    s[14] = h4 >> 10;
    s[15] = h4 >> 18;
    s[16] = h5 >> 0;
    s[17] = h5 >> 8;
    s[18] = h5 >> 16;
    s[19] = (h5 >> 24) | (h6 << 1);
    s[20] = h6 >> 7;
    s[21] = h6 >> 15;
    s[22] = (h6 >> 23) | (h7 << 3);
    s[23] = h7 >> 5;
    s[24] = h7 >> 13;
    s[25] = (h7 >> 21) | (h8 << 4);
    s[26] = h8 >> 4;
    s[27] = h8 >> 12;
    s[28] = (h8 >> 20) | (h9 << 6);
    s[29] = h9 >> 2;
    s[30] = h9 >> 10;
    s[31] = h9 >> 18;
}