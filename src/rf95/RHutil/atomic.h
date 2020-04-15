/*
* This is port of Dean Camera's ATOMIC_BLOCK macros for AVR to ARM Cortex M3 
* v1.0
* Mark Pendrith, Nov 27, 2012.
*
* From Mark:
* >When I ported the macros I emailed Dean to ask what attribution would be
* >appropriate, and here is his response:
* >
* >>Mark,
* >>I think it's great that you've ported the macros; consider them
* >>public domain, to do with whatever you wish. I hope you find them >useful .
* >>
* >>Cheers!
* >>- Dean 
*/

#ifdef __arm__ 
#ifndef _CORTEX_M3_ATOMIC_H_
#define _CORTEX_M3_ATOMIC_H_

static __inline__ uint32_t __get_primask(void) \
{ uint32_t primask = 0; \
  __asm__ volatile ("MRS %[result], PRIMASK\n\t":[result]"=r"(primask)::); \
  return primask; } // returns 0 if interrupts enabled, 1 if disabled

static __inline__ void __set_primask(uint32_t setval) \
{ __asm__ volatile ("MSR PRIMASK, %[value]\n\t""dmb\n\t""dsb\n\t""isb\n\t"::[value]"r"(setval):);
  __asm__ volatile ("" ::: "memory");}

static __inline__ uint32_t __iSeiRetVal(void) \
{ __asm__ volatile ("CPSIE i\n\t""dmb\n\t""dsb\n\t""isb\n\t"); \
  __asm__ volatile ("" ::: "memory"); return 1; }   

static __inline__ uint32_t __iCliRetVal(void) \
{ __asm__ volatile ("CPSID i\n\t""dmb\n\t""dsb\n\t""isb\n\t"); \
  __asm__ volatile ("" ::: "memory"); return 1; }   

static __inline__ void    __iSeiParam(const uint32_t *__s) \
{ __asm__ volatile ("CPSIE i\n\t""dmb\n\t""dsb\n\t""isb\n\t"); \
  __asm__ volatile ("" ::: "memory"); (void)__s; }

static __inline__ void    __iCliParam(const uint32_t *__s) \
{ __asm__ volatile ("CPSID i\n\t""dmb\n\t""dsb\n\t""isb\n\t"); \
  __asm__ volatile ("" ::: "memory"); (void)__s; }

static __inline__ void    __iRestore(const  uint32_t *__s) \
{ __set_primask(*__s); __asm__ volatile ("dmb\n\t""dsb\n\t""isb\n\t"); \
  __asm__ volatile ("" ::: "memory"); }


#define ATOMIC_BLOCK(type) \
for ( type, __ToDo = __iCliRetVal(); __ToDo ; __ToDo = 0 )

#define ATOMIC_RESTORESTATE \
uint32_t primask_save __attribute__((__cleanup__(__iRestore)))  = __get_primask()

#define ATOMIC_FORCEON \
uint32_t primask_save __attribute__((__cleanup__(__iSeiParam))) = 0
   
#define NONATOMIC_BLOCK(type) \
for ( type, __ToDo = __iSeiRetVal(); __ToDo ;  __ToDo = 0 )
   
#define NONATOMIC_RESTORESTATE \
uint32_t primask_save __attribute__((__cleanup__(__iRestore))) = __get_primask()

#define NONATOMIC_FORCEOFF \
uint32_t primask_save __attribute__((__cleanup__(__iCliParam))) = 0

#endif 
#endif
