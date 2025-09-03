#ifndef KYBER_WRAPPER_H
#define KYBER_WRAPPER_H


extern "C" {
    #include <kem.h>
    #include <indcpa.h>
    #include <poly.h>
    #include <polyvec.h>
    #include <ntt.h>
    #include <reduce.h>
    #include <verify.h>
    #include <randombytes.h>
    #include <fips202.h>
    #include <symmetric.h>
    #include <cbd.h>
    #include <aes256ctr.h>
}
#endif // KYBER_WRAPPER_H
