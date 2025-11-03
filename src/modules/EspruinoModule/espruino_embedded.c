/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2022 Gordon Williams <gw@pur3.co.uk>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * ----------------------------------------------------------------------------
 * Espruino 'embedded' single-file JS interpreter
 * ----------------------------------------------------------------------------
 */

#include "espruino_embedded.h"
typedef uint32_t JsfWord;
    typedef uint16_t JsVarRef;
    typedef int16_t JsVarRefSigned;
typedef uint16_t JsVarRefCounter;
typedef int64_t JsSysTime;
static bool isWhitespaceInline(char ch) {
    return (ch==0x09) ||
           (ch==0x0B) ||
           (ch==0x0C) ||
           (ch==0x20) ||
           (ch=='\n') ||
           (ch=='\r');
}
static bool isAlphaInline(char ch) {
    return ((ch>='a') && (ch<='z')) || ((ch>='A') && (ch<='Z')) || ch=='_';
}
static bool isNumericInline(char ch) {
    return (ch>='0') && (ch<='9');
}
bool isWhitespace(char ch);
bool isNumeric(char ch);
bool isHexadecimal(char ch);
bool isAlpha(char ch);
bool isIDString(const char *s);
char charToUpperCase(char ch);
char charToLowerCase(char ch);
const char *escapeCharacter(int ch, int nextCh, bool jsonStyle);
int getRadix(const char **s);
int chtod(char ch);
int hexToByte(char hi, char lo);
long long stringToIntWithRadix(const char *s,
               int forceRadix,
               bool *hasError,
               const char **endOfInteger
               );
long long stringToInt(const char *s);
struct JsLex;
void jsAssertFail(const char *file, int line, const char *expr);
void jsExceptionHere(JsExceptionType type, const char *fmt, ...);
void jsError(const char *fmt, ...);
void jsWarn(const char *fmt, ...);
typedef enum {
  JSERR_NONE = 0,
  JSERR_RX_FIFO_FULL = 1,
  JSERR_BUFFER_FULL = 2,
  JSERR_CALLBACK = 4,
  JSERR_LOW_MEMORY = 8,
  JSERR_MEMORY = 16,
  JSERR_MEMORY_BUSY = 32,
  JSERR_UART_OVERFLOW = 64,
  JSERR_WARNINGS_MASK = JSERR_LOW_MEMORY
} __attribute__ ((__packed__)) JsErrorFlags;
extern volatile JsErrorFlags jsErrorFlags;
JsVarFloat stringToFloatWithRadix(
    const char *s,
    int forceRadix,
    const char **endOfFloat
  );
JsVarFloat stringToFloat(const char *str);
void itostr_extra(JsVarInt vals,char *str,bool signedVal,unsigned int base);
static void itostr(JsVarInt val,char *str,unsigned int base) {
    itostr_extra(val, str, true, base);
}
char itoch(int val);
void ftoa_bounded_extra(JsVarFloat val,char *str, size_t len, int radix, int fractionalDigits);
void ftoa_bounded(JsVarFloat val,char *str, size_t len);
JsVarFloat wrapAround(JsVarFloat val, JsVarFloat size);
typedef void (*vcbprintf_callback)(const char *str, void *user_data);
void vcbprintf(vcbprintf_callback user_callback, void *user_data, const char *fmt, va_list argp);
void cbprintf(vcbprintf_callback user_callback, void *user_data, const char *fmt, ...);
int espruino_snprintf_va( char * s, size_t n, const char * fmt, va_list argp );
int espruino_snprintf( char * s, size_t n, const char * fmt, ... );
int rand();
void srand(unsigned int seed);
char clipi8(int x);
int twosComplement(int val, unsigned char bits);
bool calculateParity(uint8_t v);
unsigned short int int_sqrt32(unsigned int x);
void reverseBytes(char *data, int len);
size_t jsuGetFreeStack();
typedef struct {
  short x,y,z;
} Vector3;
typedef enum {
    JSV_UNUSED = 0,
    JSV_ROOT = JSV_UNUSED+1,
    JSV_NULL = JSV_ROOT+1,
    JSV_ARRAY,
    JSV_ARRAYBUFFER,
    JSV_OBJECT,
    JSV_GET_SET,
    JSV_FUNCTION,
    JSV_NATIVE_FUNCTION,
    JSV_FUNCTION_RETURN,
    JSV_INTEGER,
  _JSV_NUMERIC_START = JSV_INTEGER,
    JSV_FLOAT = JSV_INTEGER+1,
    JSV_BOOLEAN = JSV_FLOAT+1,
    JSV_ARRAYBUFFERNAME,
  _JSV_NAME_START = JSV_ARRAYBUFFERNAME,
    JSV_NAME_INT = JSV_ARRAYBUFFERNAME+1,
  _JSV_NAME_INT_START = JSV_NAME_INT,
    JSV_NAME_INT_INT = JSV_NAME_INT+1,
  _JSV_NAME_WITH_VALUE_START = JSV_NAME_INT_INT,
    JSV_NAME_INT_BOOL = JSV_NAME_INT_INT+1,
  _JSV_NAME_INT_END = JSV_NAME_INT_BOOL,
  _JSV_NUMERIC_END = JSV_NAME_INT_BOOL,
    JSV_NAME_STRING_INT_0 = JSV_NAME_INT_BOOL+1,
  _JSV_STRING_START = JSV_NAME_STRING_INT_0,
    JSV_NAME_STRING_INT_MAX = JSV_NAME_STRING_INT_0+4,
  _JSV_NAME_WITH_VALUE_END = JSV_NAME_STRING_INT_MAX,
    JSV_NAME_STRING_0,
    JSV_NAME_STRING_MAX = JSV_NAME_STRING_0+4,
  _JSV_NAME_END = JSV_NAME_STRING_MAX,
    JSV_STRING_0 = JSV_NAME_STRING_MAX+1,
    JSV_STRING_MAX = JSV_STRING_0+(4 + ((14*3 + 0)>>3)),
    JSV_FLAT_STRING = JSV_STRING_MAX+1,
    JSV_NATIVE_STRING = JSV_FLAT_STRING+1,
    _JSV_STRING_END = JSV_NATIVE_STRING,
    JSV_STRING_EXT_0 = _JSV_STRING_END+1,
    JSV_STRING_EXT_MAX = JSV_STRING_EXT_0+(4 + ((14*3 + 0 + 8)>>3)),
    _JSV_VAR_END = JSV_STRING_EXT_MAX,
    JSV_VARTYPEMASK = (((_JSV_VAR_END) | (_JSV_VAR_END)>>1 | (_JSV_VAR_END)>>2 | (_JSV_VAR_END)>>3 | (_JSV_VAR_END)>>4 | (_JSV_VAR_END)>>5 | (_JSV_VAR_END)>>6 | (_JSV_VAR_END)>>7 | (_JSV_VAR_END)>>8 | (_JSV_VAR_END)>>9 | (_JSV_VAR_END)>>10 | (_JSV_VAR_END)>>11 | (_JSV_VAR_END)>>12 | (_JSV_VAR_END)>>13 | (_JSV_VAR_END)>>14 | (_JSV_VAR_END)>>15)+1)-1,
    JSV_CONSTANT = JSV_VARTYPEMASK+1,
    JSV_NATIVE = JSV_CONSTANT<<1,
    JSV_GARBAGE_COLLECT = JSV_NATIVE<<1,
    JSV_IS_RECURSING = JSV_GARBAGE_COLLECT<<1,
    JSV_LOCK_ONE = JSV_IS_RECURSING<<1,
    JSV_LOCK_MASK = 15 * JSV_LOCK_ONE,
    JSV_LOCK_SHIFT = (((JSV_LOCK_ONE)== 1)? 0: ((JSV_LOCK_ONE)== 2)? 1: ((JSV_LOCK_ONE)== 4)? 2: ((JSV_LOCK_ONE)== 8)? 3: ((JSV_LOCK_ONE)== 16)? 4: ((JSV_LOCK_ONE)== 32)? 5: ((JSV_LOCK_ONE)== 64)? 6: ((JSV_LOCK_ONE)== 128)? 7: ((JSV_LOCK_ONE)== 256)? 8: ((JSV_LOCK_ONE)== 512)? 9: ((JSV_LOCK_ONE)== 1024)?10: ((JSV_LOCK_ONE)== 2048)?11: ((JSV_LOCK_ONE)== 4096)?12: ((JSV_LOCK_ONE)== 8192)?13: ((JSV_LOCK_ONE)==16384)?14: ((JSV_LOCK_ONE)==32768)?15:10000 ),
    JSV_VARIABLEINFOMASK = JSV_VARTYPEMASK | JSV_NATIVE | JSV_CONSTANT,
} __attribute__ ((__packed__)) JsVarFlags;
typedef enum {
  ARRAYBUFFERVIEW_UNDEFINED = 0,
  ARRAYBUFFERVIEW_MASK_SIZE = 15,
  ARRAYBUFFERVIEW_SIGNED = 16,
  ARRAYBUFFERVIEW_FLOAT = 32,
  ARRAYBUFFERVIEW_CLAMPED = 64,
  ARRAYBUFFERVIEW_ARRAYBUFFER = 1 | 128,
  ARRAYBUFFERVIEW_UINT8 = 1,
  ARRAYBUFFERVIEW_INT8 = 1 | ARRAYBUFFERVIEW_SIGNED,
  ARRAYBUFFERVIEW_UINT16 = 2,
  ARRAYBUFFERVIEW_INT16 = 2 | ARRAYBUFFERVIEW_SIGNED,
  ARRAYBUFFERVIEW_UINT24 = 3,
  ARRAYBUFFERVIEW_UINT32 = 4,
  ARRAYBUFFERVIEW_INT32 = 4 | ARRAYBUFFERVIEW_SIGNED,
  ARRAYBUFFERVIEW_FLOAT32 = 4 | ARRAYBUFFERVIEW_FLOAT,
  ARRAYBUFFERVIEW_FLOAT64 = 8 | ARRAYBUFFERVIEW_FLOAT,
} __attribute__ ((__packed__)) JsVarDataArrayBufferViewType;
typedef uint32_t JsVarDataNativeStrLength;
typedef uint32_t JsVarArrayBufferLength;
typedef struct {
  unsigned short byteOffset;
  JsVarArrayBufferLength length : 24;
  JsVarDataArrayBufferViewType type;
} __attribute__ ((__packed__)) JsVarDataArrayBufferView;
typedef struct {
  void (*ptr)(void);
  uint16_t argTypes;
} __attribute__ ((__packed__)) JsVarDataNative;
typedef struct {
  char *ptr;
  JsVarDataNativeStrLength len;
} __attribute__ ((__packed__)) JsVarDataNativeStr;
typedef struct {
  int8_t pad[sizeof(size_t)];
  JsVarRef nextSibling : 14;
  JsVarRef prevSibling : 14;
  JsVarRef firstChild : 14;
  JsVarRefCounter refs : 8;
  JsVarRef lastChild : 14;
} __attribute__ ((__packed__)) JsVarDataRef;
typedef union {
    char str[(4 + ((14*3 + 0 + 8)>>3))];
    JsVarInt integer;
    JsVarFloat floating;
    JsVarDataArrayBufferView arraybuffer;
    JsVarDataNative native;
    JsVarDataNativeStr nativeStr;
    JsVarDataRef ref;
} __attribute__ ((__packed__)) JsVarData;
struct JsVarStruct {
  JsVarData varData;
  volatile JsVarFlags flags;
} __attribute__ ((__packed__));
               JsVarRef jsvGetFirstChild(const JsVar *v);
               JsVarRefSigned jsvGetFirstChildSigned(const JsVar *v);
               JsVarRef jsvGetLastChild(const JsVar *v);
               JsVarRef jsvGetNextSibling(const JsVar *v);
               JsVarRef jsvGetPrevSibling(const JsVar *v);
               void jsvSetFirstChild(JsVar *v, JsVarRef r);
               void jsvSetLastChild(JsVar *v, JsVarRef r);
               void jsvSetNextSibling(JsVar *v, JsVarRef r);
               void jsvSetPrevSibling(JsVar *v, JsVarRef r);
               JsVarRefCounter jsvGetRefs(JsVar *v);
               void jsvSetRefs(JsVar *v, JsVarRefCounter refs);
               unsigned char jsvGetLocks(JsVar *v);
void jsvSetMaxVarsUsed(unsigned int size);
void jsvInit(unsigned int size);
void jsvReset();
void jsvKill();
void jsvSoftInit();
void jsvSoftKill();
JsVar *jsvFindOrCreateRoot();
unsigned int jsvGetMemoryUsage();
unsigned int jsvGetMemoryTotal();
bool jsvIsMemoryFull();
bool jsvMoreFreeVariablesThan(unsigned int vars);
void jsvShowAllocated();
void jsvSetMemoryTotal(unsigned int jsNewVarCount);
void jsvUpdateMemoryAddress(size_t oldAddr, size_t length, size_t newAddr);
JsVar *jsvNewWithFlags(JsVarFlags flags);
JsVar *jsvNewFlatStringOfLength(unsigned int byteLength);
JsVar *jsvNewFromString(const char *str);
JsVar *jsvNewNameFromString(const char *str);
JsVar *jsvNewStringOfLength(unsigned int byteLength, const char *initialData);
static JsVar *jsvNewFromEmptyString() { return jsvNewWithFlags(JSV_STRING_0); } ;
static JsVar *jsvNewNull() { return jsvNewWithFlags(JSV_NULL); } ;
JsVar *jsvNewFlatStringFromStringVar(JsVar *var, size_t stridx, size_t maxLength);
JsVar *jsvNewWritableStringFromStringVar(const JsVar *str, size_t stridx, size_t maxLength);
JsVar *jsvNewFromStringVar(const JsVar *str, size_t stridx, size_t maxLength);
JsVar *jsvNewFromStringVarComplete(JsVar *var);
JsVar *jsvNewFromInteger(JsVarInt value);
JsVar *jsvNewFromBool(bool value);
JsVar *jsvNewFromFloat(JsVarFloat value);
JsVar *jsvNewFromLongInteger(long long value);
JsVar *jsvNewObject();
JsVar *jsvNewEmptyArray();
JsVar *jsvNewArray(JsVar **elements, int elementCount);
JsVar *jsvNewArrayFromBytes(uint8_t *elements, int elementCount);
JsVar *jsvNewNativeFunction(void (*ptr)(void), unsigned short argTypes);
JsVar *jsvNewNativeString(char *ptr, size_t len);
JsVar *jsvNewArrayBufferFromString(JsVar *str, unsigned int lengthOrZero);
JsVar *jsvMakeIntoVariableName(JsVar *var, JsVar *valueOrZero);
JsVar *jsvMakeFunctionParameter(JsVar *v);
void jsvAddFunctionParameter(JsVar *fn, JsVar *name, JsVar *value);
void *jsvGetNativeFunctionPtr(const JsVar *function);
               JsVarRef jsvGetRef(JsVar *var);
               JsVar *_jsvGetAddressOf(JsVarRef ref);
               JsVar *jsvLock(JsVarRef ref);
JsVar *jsvLockSafe(JsVarRef ref);
               JsVar *jsvLockAgain(JsVar *var);
               JsVar *jsvLockAgainSafe(JsVar *var);
               void jsvUnLock(JsVar *var);
__attribute__ ((noinline)) void jsvUnLock2(JsVar *var1, JsVar *var2);
__attribute__ ((noinline)) void jsvUnLock3(JsVar *var1, JsVar *var2, JsVar *var3);
__attribute__ ((noinline)) void jsvUnLock4(JsVar *var1, JsVar *var2, JsVar *var3, JsVar *var4);
__attribute__ ((noinline)) void jsvUnLockMany(unsigned int count, JsVar **vars);
JsVar *jsvRef(JsVar *v);
void jsvUnRef(JsVar *var);
JsVarRef jsvRefRef(JsVarRef ref);
JsVarRef jsvUnRefRef(JsVarRef ref);
bool jsvIsRoot(const JsVar *v);
bool jsvIsPin(const JsVar *v);
bool jsvIsSimpleInt(const JsVar *v);
bool jsvIsInt(const JsVar *v);
bool jsvIsFloat(const JsVar *v);
bool jsvIsBoolean(const JsVar *v);
bool jsvIsString(const JsVar *v);
bool jsvIsUTF8String(const JsVar *v);
bool jsvIsBasicString(const JsVar *v);
bool jsvIsStringExt(const JsVar *v);
bool jsvIsFlatString(const JsVar *v);
bool jsvIsNativeString(const JsVar *v);
bool jsvIsConstant(const JsVar *v);
bool jsvIsFlashString(const JsVar *v);
bool jsvIsNumeric(const JsVar *v);
bool jsvIsFunction(const JsVar *v);
bool jsvIsFunctionReturn(const JsVar *v);
bool jsvIsFunctionParameter(const JsVar *v);
bool jsvIsObject(const JsVar *v);
bool jsvIsArray(const JsVar *v);
bool jsvIsArrayBuffer(const JsVar *v);
bool jsvIsArrayBufferName(const JsVar *v);
bool jsvIsNativeFunction(const JsVar *v);
bool jsvIsUndefined(const JsVar *v);
bool jsvIsNull(const JsVar *v);
bool jsvIsNullish(const JsVar *v);
bool jsvIsBasic(const JsVar *v);
bool jsvIsName(const JsVar *v);
bool jsvIsBasicName(const JsVar *v);
bool jsvIsNameWithValue(const JsVar *v);
bool jsvIsNameInt(const JsVar *v);
bool jsvIsNameIntInt(const JsVar *v);
bool jsvIsNameIntBool(const JsVar *v);
bool jsvIsNewChild(const JsVar *v);
bool jsvIsGetterOrSetter(const JsVar *v);
bool jsvIsRefUsedForData(const JsVar *v);
bool jsvIsIntegerish(const JsVar *v);
bool jsvIsIterable(const JsVar *v);
bool jsvIsStringNumericInt(const JsVar *var, bool allowDecimalPoint);
bool jsvIsStringNumericStrict(const JsVar *var);
bool jsvHasCharacterData(const JsVar *v);
bool jsvHasStringExt(const JsVar *v);
bool jsvHasChildren(const JsVar *v);
bool jsvHasSingleChild(const JsVar *v);
JsVar *jsvCreateNewChild(JsVar *parent, JsVar *index, JsVar *child);
static bool jsvHasRef(const JsVar *v) { return !jsvIsStringExt(v); }
size_t jsvGetMaxCharactersInVar(const JsVar *v);
size_t jsvGetCharactersInVar(const JsVar *v);
void jsvSetCharactersInVar(JsVar *v, size_t chars);
bool jsvIsBasicVarEqual(JsVar *a, JsVar *b);
bool jsvIsEqual(JsVar *a, JsVar *b);
const char *jsvGetConstString(const JsVar *v);
const char *jsvGetTypeOf(const JsVar *v);
JsVar *jsvGetValueOf(JsVar *v);
size_t jsvGetString(const JsVar *v, char *str, size_t len);
size_t jsvGetStringChars(const JsVar *v, size_t startChar, char *str, size_t len);
void jsvSetString(JsVar *v, const char *str, size_t len);
JsVar *jsvAsString(JsVar *var);
JsVar *jsvAsStringAndUnLock(JsVar *var);
JsVar *jsvAsFlatString(JsVar *var);
bool jsvIsEmptyString(JsVar *v);
size_t jsvGetStringLength(const JsVar *v);
size_t jsvGetFlatStringBlocks(const JsVar *v);
char *jsvGetFlatStringPointer(JsVar *v);
JsVar *jsvGetFlatStringFromPointer(char *v);
char *jsvGetDataPointer(JsVar *v, size_t *len);
size_t jsvGetLinesInString(JsVar *v);
size_t jsvGetCharsOnLine(JsVar *v, size_t line);
void jsvGetLineAndCol(JsVar *v, size_t charIdx, size_t *line, size_t *col, size_t *ignoredLines);
size_t jsvGetIndexFromLineAndCol(JsVar *v, size_t line, size_t col);
bool jsvIsStringEqualOrStartsWithOffset(JsVar *var, const char *str, bool isStartsWith, size_t startIdx, bool ignoreCase);
bool jsvIsStringEqualOrStartsWith(JsVar *var, const char *str, bool isStartsWith);
bool jsvIsStringEqual(JsVar *var, const char *str);
bool jsvIsStringIEqualAndUnLock(JsVar *var, const char *str);
int jsvCompareString(JsVar *va, JsVar *vb, size_t starta, size_t startb, bool equalAtEndOfString);
JsVar *jsvGetCommonCharacters(JsVar *va, JsVar *vb);
int jsvCompareInteger(JsVar *va, JsVar *vb);
void jsvAppendString(JsVar *var, const char *str);
void jsvAppendStringBuf(JsVar *var, const char *str, size_t length);
void jsvAppendPrintf(JsVar *var, const char *fmt, ...);
JsVar *jsvVarPrintf( const char *fmt, ...);
static void jsvAppendCharacter(JsVar *var, char ch) { jsvAppendStringBuf(var, &ch, 1); };
void jsvAppendStringVar(JsVar *var, const JsVar *str, size_t stridx, size_t maxLength);
void jsvAppendStringVarComplete(JsVar *var, const JsVar *str);
int jsvGetCharInString(JsVar *v, size_t idx);
void jsvSetCharInString(JsVar *v, size_t idx, char ch, bool bitwiseOR);
int jsvGetStringIndexOf(JsVar *str, char ch);
int jsvConvertFromUTF8Index(JsVar *str, int idx);
int jsvConvertToUTF8Index(JsVar *str, int idx);
JsVarInt jsvGetInteger(const JsVar *v);
void jsvSetInteger(JsVar *v, JsVarInt value);
JsVarFloat jsvGetFloat(const JsVar *v);
bool jsvGetBool(const JsVar *v);
long long jsvGetLongInteger(const JsVar *v);
JsVar *jsvAsNumber(JsVar *var);
JsVar *jsvAsNumberAndUnLock(JsVar *v);
static JsVarInt _jsvGetIntegerAndUnLock(JsVar *v) { JsVarInt i = jsvGetInteger(v); jsvUnLock(v); return i; }
static JsVarFloat _jsvGetFloatAndUnLock(JsVar *v) { JsVarFloat f = jsvGetFloat(v); jsvUnLock(v); return f; }
static bool _jsvGetBoolAndUnLock(JsVar *v) { bool b = jsvGetBool(v); jsvUnLock(v); return b; }
JsVarInt jsvGetIntegerAndUnLock(JsVar *v);
JsVarFloat jsvGetFloatAndUnLock(JsVar *v);
bool jsvGetBoolAndUnLock(JsVar *v);
long long jsvGetLongIntegerAndUnLock(JsVar *v);
JsVar *jsvExecuteGetter(JsVar *parent, JsVar *getset);
void jsvExecuteSetter(JsVar *parent, JsVar *getset, JsVar *value);
void jsvAddGetterOrSetter(JsVar *obj, JsVar *varName, bool isGetter, JsVar *method);
void jsvReplaceWith(JsVar *dst, JsVar *src);
void jsvReplaceWithOrAddToRoot(JsVar *dst, JsVar *src);
size_t jsvGetArrayBufferLength(const JsVar *arrayBuffer);
JsVar *jsvGetArrayBufferBackingString(JsVar *arrayBuffer, uint32_t *offset);
JsVar *jsvArrayBufferGet(JsVar *arrayBuffer, size_t index);
void jsvArrayBufferSet(JsVar *arrayBuffer, size_t index, JsVar *value);
JsVar *jsvArrayBufferGetFromName(JsVar *name);
JsVar *jsvGetFunctionArgumentLength(JsVar *function);
bool jsvIsVariableDefined(JsVar *a);
JsVar *jsvGetValueOfName(JsVar *name);
void jsvCheckReferenceError(JsVar *a);
JsVar *jsvSkipNameWithParent(JsVar *a, bool repeat, JsVar *parent);
JsVar *jsvSkipName(JsVar *a);
JsVar *jsvSkipOneName(JsVar *a);
JsVar *jsvSkipToLastName(JsVar *a);
JsVar *jsvSkipNameAndUnLock(JsVar *a);
JsVar *jsvSkipOneNameAndUnLock(JsVar *a);
JsVar *jsvAsArrayIndex(JsVar *index);
JsVar *jsvAsArrayIndexAndUnLock(JsVar *a);
JsVar *jsvAsName(JsVar *var);
JsVar *jsvMathsOpSkipNames(JsVar *a, JsVar *b, int op);
bool jsvMathsOpTypeEqual(JsVar *a, JsVar *b);
JsVar *jsvMathsOp(JsVar *a, JsVar *b, int op);
JsVar *jsvNegateAndUnLock(JsVar *v);
JsVar *jsvGetPathTo(JsVar *root, JsVar *element, int maxDepth, JsVar *ignoreParent);
JsVar *jsvCopy(JsVar *src, bool copyChildren);
JsVar *jsvCopyNameOnly(JsVar *src, bool linkChildren, bool keepAsName);
void jsvAddName(JsVar *parent, JsVar *nameChild);
JsVar *jsvAddNamedChild(JsVar *parent, JsVar *value, const char *name);
void jsvAddNamedChildAndUnLock(JsVar *parent, JsVar *value, const char *name);
JsVar *jsvSetValueOfName(JsVar *name, JsVar *src);
JsVar *jsvFindChildFromString(JsVar *parent, const char *name);
JsVar *jsvFindOrAddChildFromString(JsVar *parent, const char *name);
JsVar *jsvFindChildFromStringI(JsVar *parent, const char *name);
JsVar *jsvFindChildFromVar(JsVar *parent, JsVar *childName, bool addIfNotFound);
void jsvRemoveChild(JsVar *parent, JsVar *child);
void jsvRemoveChildAndUnLock(JsVar *parent, JsVar *child);
void jsvRemoveAllChildren(JsVar *parent);
JsVar *jsvObjectGetChild(JsVar *obj, const char *name, JsVarFlags createChild);
JsVar *jsvObjectGetChildIfExists(JsVar *obj, const char *name);
JsVar *jsvObjectGetChildI(JsVar *obj, const char *name);
bool jsvObjectGetBoolChild(JsVar *obj, const char *name);
JsVarInt jsvObjectGetIntegerChild(JsVar *obj, const char *name);
JsVarFloat jsvObjectGetFloatChild(JsVar *obj, const char *name);
JsVar *jsvObjectSetChild(JsVar *obj, const char *name, JsVar *child);
JsVar *jsvObjectSetChildVar(JsVar *obj, JsVar *name, JsVar *child);
void jsvObjectSetChildAndUnLock(JsVar *obj, const char *name, JsVar *child);
void jsvObjectRemoveChild(JsVar *parent, const char *name);
JsVar *jsvObjectSetOrRemoveChild(JsVar *obj, const char *name, JsVar *child);
void jsvObjectAppendAll(JsVar *target, JsVar *source);
int jsvGetChildren(const JsVar *v);
JsVar *jsvGetFirstName(JsVar *v);
bool jsvIsChild(JsVar *parent, JsVar *child);
JsVarInt jsvGetArrayLength(const JsVar *arr);
JsVarInt jsvSetArrayLength(JsVar *arr, JsVarInt length, bool truncate);
JsVarInt jsvGetLength(const JsVar *src);
size_t jsvCountJsVarsUsed(JsVar *v);
JsVar *jsvGetArrayIndex(const JsVar *arr, JsVarInt index);
JsVar *jsvGetArrayItem(const JsVar *arr, JsVarInt index);
JsVar *jsvGetLastArrayItem(const JsVar *arr);
void jsvSetArrayItem(JsVar *arr, JsVarInt index, JsVar *item);
void jsvGetArrayItems(JsVar *arr, unsigned int itemCount, JsVar **itemPtr);
JsVar *jsvGetIndexOfFull(JsVar *arr, JsVar *value, bool matchExact, bool matchIntegerIndices, int startIdx);
JsVar *jsvGetIndexOf(JsVar *arr, JsVar *value, bool matchExact);
JsVarInt jsvArrayAddToEnd(JsVar *arr, JsVar *value, JsVarInt initialValue);
JsVarInt jsvArrayPush(JsVar *arr, JsVar *value);
JsVarInt jsvArrayPushAndUnLock(JsVar *arr, JsVar *value);
JsVarInt jsvArrayPushString(JsVar *arr, const char *string);
void jsvArrayPush2Int(JsVar *arr, JsVarInt a, JsVarInt b);
void jsvArrayPushAll(JsVar *target, JsVar *source, bool checkDuplicates);
JsVar *jsvArrayPop(JsVar *arr);
JsVar *jsvArrayPopFirst(JsVar *arr);
void jsvArrayAddUnique(JsVar *arr, JsVar *v);
JsVar *jsvArrayJoin(JsVar *arr, JsVar *filler, bool ignoreNull);
void jsvArrayInsertBefore(JsVar *arr, JsVar *beforeIndex, JsVar *element);
static bool jsvArrayIsEmpty(JsVar *arr) { do { if (!(jsvIsArray(arr))) jsAssertFail("/Volumes/Code/repos/meshenvy/meshtastic/firmware/Espruino/src/jsvar.h",756,""); } while(0); return !jsvGetFirstChild(arr); }
void jsvTrace(JsVar *var, int indent);
int jsvGarbageCollect();
void jsvDefragment();
void jsvDumpLockedVars();
void jsvDumpFreeList();
JsVar *jsvStringTrimRight(JsVar *srcString);
typedef bool (*JsvIsInternalChecker)(JsVar*);
bool jsvIsInternalFunctionKey(JsVar *v);
bool jsvIsInternalObjectKey(JsVar *v);
JsvIsInternalChecker jsvGetInternalFunctionCheckerFor(JsVar *v);
typedef struct {
  char *name;
  JsVarFlags type;
  void *ptr;
} jsvConfigObject;
bool jsvReadConfigObject(JsVar *object, jsvConfigObject *configs, int nConfigs);
JsVar *jsvCreateConfigObject(jsvConfigObject *configs, int nConfigs);
bool jsvIsInstanceOf(JsVar *var, const char *constructorName);
JsVar *jsvNewTypedArray(JsVarDataArrayBufferViewType type, JsVarInt length);
JsVar *jsvNewDataViewWithData(JsVarInt length, unsigned char *data);
JsVar *jsvNewArrayBufferWithPtr(unsigned int length, char **ptr);
JsVar *jsvNewArrayBufferWithData(JsVarInt length, unsigned char *data);
void *jsvMalloc(size_t size);
void jsvFree(void *ptr);
extern unsigned int jsVarsSize;
extern JsVar *jsVars;
typedef void (*jsvIterateCallbackFn)(int item, void *callbackData);
typedef void (*jsvIterateBufferCallbackFn)(unsigned char *data, unsigned int len, void *callbackData);
bool jsvIterateCallback(JsVar *var, jsvIterateCallbackFn callback, void *callbackData);
bool jsvIterateBufferCallback(
    JsVar *data,
    jsvIterateBufferCallbackFn callback,
    void *callbackData
  );
uint32_t jsvIterateCallbackCount(JsVar *var);
unsigned int jsvIterateCallbackToBytes(JsVar *var, unsigned char *data, unsigned int dataSize);
typedef struct JsvStringIterator {
  size_t charIdx;
  size_t charsInVar;
  size_t varIndex;
  JsVar *var;
  char *ptr;
} JsvStringIterator;
void jsvStringIteratorNew(JsvStringIterator *it, JsVar *str, size_t startIdx);
void jsvStringIteratorNewUTF8(JsvStringIterator *it, JsVar *str, size_t startIdx);
void jsvStringIteratorUpdatePtr(JsvStringIterator *it);
void jsvStringIteratorClone(JsvStringIterator *dstit, JsvStringIterator *it);
static char jsvStringIteratorGetChar(JsvStringIterator *it) {
  if (!it->ptr) return 0;
  return (char)(*(uint8_t*)(&it->ptr[it->charIdx]));
}
char jsvStringIteratorGetCharAndNext(JsvStringIterator *it);
int jsvStringIteratorGetUTF8CharAndNext(JsvStringIterator *it);
int jsvStringIteratorGetCharOrMinusOne(JsvStringIterator *it);
static bool jsvStringIteratorHasChar(JsvStringIterator *it) {
  return it->charIdx < it->charsInVar;
}
void jsvStringIteratorSetChar(JsvStringIterator *it, char c);
void jsvStringIteratorSetCharAndNext(JsvStringIterator *it, char c);
static size_t jsvStringIteratorGetIndex(JsvStringIterator *it) {
  return it->varIndex + it->charIdx;
}
void jsvStringIteratorNext(JsvStringIterator *it);
void jsvStringIteratorNextUTF8(JsvStringIterator *it);
void jsvStringIteratorGetPtrAndNext(JsvStringIterator *it, unsigned char **data, unsigned int *len);
static void jsvStringIteratorLoadInline(JsvStringIterator *it) {
  it->charIdx -= it->charsInVar;
  it->varIndex += it->charsInVar;
  if (it->var && jsvGetLastChild(it->var)) {
    JsVar *next = jsvLock(jsvGetLastChild(it->var));
    jsvUnLock(it->var);
    it->var = next;
    it->ptr = &next->varData.str[0];
    it->charsInVar = jsvGetCharactersInVar(it->var);
  } else {
    jsvUnLock(it->var);
    it->var = 0;
    it->ptr = 0;
    it->charsInVar = 0;
    it->varIndex += it->charIdx;
    it->charIdx = 0;
  }
}
static void jsvStringIteratorNextInline(JsvStringIterator *it) {
  it->charIdx++;
  if (it->charIdx >= it->charsInVar) {
    jsvStringIteratorLoadInline(it);
  }
}
void jsvStringIteratorGotoEnd(JsvStringIterator *it);
void jsvStringIteratorGoto(JsvStringIterator *it, JsVar *str, size_t startIdx);
void jsvStringIteratorGotoUTF8(JsvStringIterator *it, JsVar *str, size_t idx);
void jsvStringIteratorAppend(JsvStringIterator *it, char ch);
void jsvStringIteratorAppendString(JsvStringIterator *it, JsVar *str, size_t startIdx, int maxLength);
static void jsvStringIteratorFree(JsvStringIterator *it) {
  jsvUnLock(it->var);
}
void jsvStringIteratorPrintfCallback(const char *str, void *user_data);
typedef struct JsvObjectIterator {
  JsVar *var;
} JsvObjectIterator;
void jsvObjectIteratorNew(JsvObjectIterator *it, JsVar *obj);
void jsvObjectIteratorClone(JsvObjectIterator *dstit, JsvObjectIterator *it);
static JsVar *jsvObjectIteratorGetKey(JsvObjectIterator *it) {
  if (!it->var) return 0;
  return jsvLockAgain(it->var);
}
static JsVar *jsvObjectIteratorGetValue(JsvObjectIterator *it) {
  if (!it->var) return 0;
  return jsvSkipName(it->var);
}
static bool jsvObjectIteratorHasValue(JsvObjectIterator *it) {
  return it->var != 0;
}
void jsvObjectIteratorSetValue(JsvObjectIterator *it, JsVar *value);
void jsvObjectIteratorNext(JsvObjectIterator *it);
void jsvObjectIteratorRemoveAndGotoNext(JsvObjectIterator *it, JsVar *parent);
static void jsvObjectIteratorFree(JsvObjectIterator *it) {
  jsvUnLock(it->var);
}
typedef struct JsvArrayBufferIterator {
  JsvStringIterator it;
  JsVarDataArrayBufferViewType type;
  size_t byteLength;
  size_t byteOffset;
  size_t index;
  bool hasAccessedElement;
} JsvArrayBufferIterator;
void jsvArrayBufferIteratorNew(JsvArrayBufferIterator *it, JsVar *arrayBuffer, size_t index);
void jsvArrayBufferIteratorClone(JsvArrayBufferIterator *dstit, JsvArrayBufferIterator *it);
JsVar *jsvArrayBufferIteratorGetValue(JsvArrayBufferIterator *it, bool bigEndian);
JsVar *jsvArrayBufferIteratorGetValueAndRewind(JsvArrayBufferIterator *it);
JsVarInt jsvArrayBufferIteratorGetIntegerValue(JsvArrayBufferIterator *it);
JsVarFloat jsvArrayBufferIteratorGetFloatValue(JsvArrayBufferIterator *it);
void jsvArrayBufferIteratorSetValue(JsvArrayBufferIterator *it, JsVar *value, bool bigEndian);
void jsvArrayBufferIteratorSetValueAndRewind(JsvArrayBufferIterator *it, JsVar *value);
void jsvArrayBufferIteratorSetIntegerValue(JsvArrayBufferIterator *it, JsVarInt value);
void jsvArrayBufferIteratorSetByteValue(JsvArrayBufferIterator *it, char c);
JsVar* jsvArrayBufferIteratorGetIndex(JsvArrayBufferIterator *it);
bool jsvArrayBufferIteratorHasElement(JsvArrayBufferIterator *it);
void jsvArrayBufferIteratorNext(JsvArrayBufferIterator *it);
void jsvArrayBufferIteratorFree(JsvArrayBufferIterator *it);
typedef struct {
  JsvObjectIterator it;
  JsVar *var;
  JsVarInt index;
} JsvIteratorObj;
typedef struct {
  JsvStringIterator str;
  JsVarInt index;
  int currentCh;
} JsvIteratorUTF8;
union JsvIteratorUnion {
  JsvStringIterator str;
  JsvIteratorObj obj;
  JsvArrayBufferIterator buf;
  JsvIteratorUTF8 unicode;
};
typedef struct JsvIterator {
  enum {
    JSVI_NONE,
    JSVI_STRING,
    JSVI_OBJECT,
    JSVI_ARRAYBUFFER,
    JSVI_FULLARRAY,
    JSVI_UNICODE
  } type;
  union JsvIteratorUnion it;
} JsvIterator;
typedef enum {
  JSIF_DEFINED_ARRAY_ElEMENTS = 0,
  JSIF_EVERY_ARRAY_ELEMENT = 1,
} JsvIteratorFlags;
void jsvIteratorNew(JsvIterator *it, JsVar *obj, JsvIteratorFlags flags);
JsVar *jsvIteratorGetKey(JsvIterator *it);
JsVar *jsvIteratorGetValue(JsvIterator *it);
JsVarInt jsvIteratorGetIntegerValue(JsvIterator *it);
JsVarFloat jsvIteratorGetFloatValue(JsvIterator *it);
JsVar *jsvIteratorSetValue(JsvIterator *it, JsVar *value);
bool jsvIteratorHasElement(JsvIterator *it);
void jsvIteratorNext(JsvIterator *it);
void jsvIteratorFree(JsvIterator *it);
void jsvIteratorClone(JsvIterator *dstit, JsvIterator *it);

typedef enum LEX_TYPES {
    LEX_EOF = 0,
    LEX_TOKEN_START = 128,
    LEX_ID = LEX_TOKEN_START,
    LEX_INT,
    LEX_FLOAT,
    LEX_STR,
    LEX_UNFINISHED_STR,
    LEX_TEMPLATE_LITERAL,
    LEX_UNFINISHED_TEMPLATE_LITERAL,
    LEX_REGEX,
    LEX_UNFINISHED_REGEX,
    LEX_UNFINISHED_COMMENT,
_LEX_TOKENS_START,
_LEX_OPERATOR1_START = _LEX_TOKENS_START,
    LEX_EQUAL = _LEX_OPERATOR1_START,
    LEX_TYPEEQUAL,
    LEX_NEQUAL,
    LEX_NTYPEEQUAL,
    LEX_LEQUAL,
    LEX_LSHIFT,
    LEX_LSHIFTEQUAL,
    LEX_GEQUAL,
    LEX_RSHIFT,
    LEX_RSHIFTUNSIGNED,
    LEX_RSHIFTEQUAL,
    LEX_RSHIFTUNSIGNEDEQUAL,
    LEX_PLUSEQUAL,
    LEX_MINUSEQUAL,
    LEX_PLUSPLUS,
    LEX_MINUSMINUS,
    LEX_MULEQUAL,
    LEX_DIVEQUAL,
    LEX_MODEQUAL,
    LEX_ANDEQUAL,
    LEX_ANDAND,
    LEX_OREQUAL,
    LEX_OROR,
    LEX_XOREQUAL,
_LEX_OPERATOR1_END = LEX_XOREQUAL,
    LEX_ARROW_FUNCTION,
_LEX_R_LIST_START,
    LEX_R_IF = _LEX_R_LIST_START,
    LEX_R_ELSE,
    LEX_R_DO,
    LEX_R_WHILE,
    LEX_R_FOR,
    LEX_R_BREAK,
    LEX_R_CONTINUE,
    LEX_R_FUNCTION,
    LEX_R_RETURN,
    LEX_R_VAR,
    LEX_R_LET,
    LEX_R_CONST,
    LEX_R_THIS,
    LEX_R_THROW,
    LEX_R_TRY,
    LEX_R_CATCH,
    LEX_R_FINALLY,
    LEX_R_TRUE,
    LEX_R_FALSE,
    LEX_R_NULL,
    LEX_R_UNDEFINED,
    LEX_R_NEW,
    LEX_R_IN,
    LEX_R_INSTANCEOF,
    LEX_R_SWITCH,
    LEX_R_CASE,
    LEX_R_DEFAULT,
    LEX_R_DELETE,
    LEX_R_TYPEOF,
    LEX_R_VOID,
    LEX_R_DEBUGGER,
    LEX_R_CLASS,
    LEX_R_EXTENDS,
    LEX_R_SUPER,
    LEX_R_STATIC,
    LEX_R_OF,
_LEX_R_LIST_END = LEX_R_OF,
_LEX_OPERATOR2_START = _LEX_R_LIST_END+10,
    LEX_NULLISH = _LEX_OPERATOR2_START,
    LEX_RAW_STRING8,
    LEX_RAW_STRING16,
    LEX_RAW_INT0,
    LEX_RAW_INT8,
    LEX_RAW_INT16,
_LEX_OPERATOR2_END = LEX_NULLISH,
_LEX_TOKENS_END = _LEX_OPERATOR2_END,
} LEX_TYPES;
typedef struct JslCharPos {
  JsvStringIterator it;
  char currCh;
} JslCharPos;
void jslCharPosFree(JslCharPos *pos);
void jslCharPosClone(JslCharPos *dstpos, JslCharPos *pos);
void jslCharPosClear(JslCharPos *pos);
void jslCharPosFromLex(JslCharPos *dstpos);
void jslCharPosNew(JslCharPos *dstpos, JsVar *src, size_t tokenStart);
typedef struct JsLex
{
  char currCh;
  short tk;
  size_t tokenStart;
  size_t tokenLastStart;
  char token[64];
  JsVar *tokenValue;
  unsigned char tokenl;
  bool hadThisKeyword;
  JsVar *sourceVar;
  JsvStringIterator it;
  JsVar *functionName;
  struct JsLex *lastLex;
} JsLex;
extern JsLex *lex;
JsLex *jslSetLex(JsLex *l);
void jslInit(JsVar *var);
void jslKill();
void jslReset();
void jslSeekTo(size_t seekToChar);
void jslSeekToP(JslCharPos *seekToChar);
bool jslMatch(int expected_tk);
void jslFunctionCharAsString(unsigned char ch, char *str, size_t len);
void jslTokenAsString(int token, char *str, size_t len);
void jslGetTokenString(char *str, size_t len);
char *jslGetTokenValueAsString();
size_t jslGetTokenLength();
JsVar *jslGetTokenValueAsVar();
bool jslIsIDOrReservedWord();
void jslSkipWhiteSpace();
void jslGetNextToken();
JsVar *jslNewStringFromLexer(JslCharPos *charFrom, size_t charTo);
JsVar *jslNewTokenisedStringFromLexer(JslCharPos *charFrom, size_t charTo);
bool jslNeedSpaceBetween(unsigned char lastch, unsigned char ch);
void jslPrintTokenisedString(JsVar *code, vcbprintf_callback user_callback, void *user_data);
void jslPrintPosition(vcbprintf_callback user_callback, void *user_data, JsLex *lex, size_t tokenPos);
void jslPrintTokenLineMarker(vcbprintf_callback user_callback, void *user_data, JsLex *lex, size_t tokenPos, size_t prefixLength);
void jslPrintStackTrace(vcbprintf_callback user_callback, void *user_data, JsLex *lex);
void jspInit();
void jspKill();
void jspSoftInit();
void jspSoftKill();
bool jspIsConstructor(JsVar *constructor, const char *constructorName);
JsVar *jspGetPrototype(JsVar *object);
JsVar *jspGetConstructor(JsVar *object);
bool jspCheckStackPosition();
JsVar *jspNewBuiltin(const char *name);
__attribute__ ((noinline)) JsVar *jspNewPrototype(const char *instanceOf, bool returnObject);
JsVar *jspNewObject(const char *name, const char *instanceOf);
bool jspIsInterrupted();
void jspSetInterrupted(bool interrupt);
bool jspHasError();
void jspSetError();
void jspSetException(JsVar *value);
JsVar *jspGetException();
JsVar *jspGetStackTrace();
void jspAppendStackTrace(JsVar *stackTrace, JsLex *lex);
JsVar *jspEvaluateExpressionVar(JsVar *str);
JsVar *jspEvaluateVar(JsVar *str, JsVar *scope, const char *stackTraceName);
JsVar *jspEvaluate(const char *str, bool stringIsStatic);
JsVar *jspExecuteJSFunctionCode(const char *argNames, const char *jsCode, size_t jsCodeLen, JsVar *thisArg, int argCount, JsVar **argPtr);
JsVar *jspExecuteFunction(JsVar *func, JsVar *thisArg, int argCount, JsVar **argPtr);
JsVar *jspEvaluateModule(JsVar *moduleContents);
JsVar *jspGetPrototypeOwner(JsVar *proto);
typedef enum {
  EXEC_NO = 0,
  EXEC_YES = 1,
  EXEC_BREAK = 2,
  EXEC_CONTINUE = 4,
  EXEC_RETURN = 8,
  EXEC_INTERRUPTED = 16,
  EXEC_EXCEPTION = 32,
  EXEC_ERROR = 64,
  EXEC_FOR_INIT = 256,
  EXEC_IN_LOOP = 512,
  EXEC_IN_SWITCH = 1024,
  EXEC_CTRL_C = 2048,
  EXEC_CTRL_C_WAIT = 4096,
  EXEC_DEBUGGER_MASK = 0,
  EXEC_RUN_MASK = EXEC_YES|EXEC_BREAK|EXEC_CONTINUE|EXEC_RETURN|EXEC_INTERRUPTED|EXEC_EXCEPTION,
  EXEC_ERROR_MASK = EXEC_INTERRUPTED|EXEC_ERROR|EXEC_EXCEPTION,
  EXEC_NO_PARSE_MASK = EXEC_INTERRUPTED|EXEC_ERROR,
  EXEC_SAVE_RESTORE_MASK = EXEC_YES|EXEC_BREAK|EXEC_CONTINUE|EXEC_RETURN|EXEC_IN_LOOP|EXEC_IN_SWITCH|EXEC_ERROR_MASK,
  EXEC_CTRL_C_MASK = EXEC_CTRL_C | EXEC_CTRL_C_WAIT,
  EXEC_PERSIST = EXEC_ERROR_MASK|EXEC_CTRL_C_MASK,
} JsExecFlags;
typedef struct {
  JsVar *root;
  JsVar *hiddenRoot;
  JsVar *scopesVar;
  JsVar *baseScope;
  JsVar *blockScope;
  uint8_t blockCount;
  JsVar *thisVar;
  JsVar *currentClassConstructor;
  volatile JsExecFlags execute;
} JsExecInfo;
extern JsExecInfo execInfo;
typedef enum {
  JSP_NOSKIP_A = 1,
  JSP_NOSKIP_B = 2,
  JSP_NOSKIP_C = 4,
  JSP_NOSKIP_D = 8,
  JSP_NOSKIP_E = 16,
  JSP_NOSKIP_F = 32,
  JSP_NOSKIP_G = 64,
  JSP_NOSKIP_H = 128,
} JspSkipFlags;
bool jspParseEmptyFunction();
JsVar *jspParse();
JsVar *jspeFunctionCall(JsVar *function, JsVar *functionName, JsVar *thisArg, bool isParsing, int argCount, JsVar **argPtr);
JsVar *jspGetNamedVariable(const char *tokenName);
JsVar *jspGetNamedField(JsVar *object, const char* name, bool returnName);
JsVar *jspGetVarNamedField(JsVar *object, JsVar *nameVar, bool returnName);
JsVar *jspeiFindInScopes(const char *name);
JsVar *jspeiGetTopScope();
JsVar *jswrap_global();
JsVar *jswrap_arguments();
JsVar *jswrap_function_constructor(JsVar *code);
JsVar *jswrap_eval(JsVar *v);
JsVar *jswrap_parseInt(JsVar *v, JsVar *radixVar);
JsVarFloat jswrap_parseFloat(JsVar *v);
bool jswrap_isFinite(JsVar *v);
bool jswrap_isNaN(JsVar *v);
JsVar *jswrap_btoa(JsVar *binaryData);
JsVar *jswrap_atob(JsVar *base64Data);
JsVar *jswrap_encodeURIComponent(JsVar *arg);
JsVar *jswrap_decodeURIComponent(JsVar *arg);
void jswrap_trace(JsVar *root);
void jswrap_print(JsVar *v);
void jswrap_console_trace(JsVar *v);

typedef union {
  uint32_t firstChars;
  char c[28];
} JsfFileName;
typedef struct {
  uint32_t size;
  JsfFileName name;
} JsfFileHeader;
typedef enum {
  JSFF_NONE,
  JSFF_FILENAME_TABLE = 32,
  JSFF_STORAGEFILE = 64,
  JSFF_COMPRESSED = 128
} JsfFileFlags;
JsfFileName jsfNameFromString(const char *name);
JsfFileName jsfNameFromVar(JsVar *name);
JsfFileName jsfNameFromVarAndUnLock(JsVar *name);
JsVar *jsfVarFromName(JsfFileName name);
bool jsfIsNameEqual(JsfFileName a, JsfFileName b);
uint32_t jsfGetFileSize(JsfFileHeader *header);
JsfFileFlags jsfGetFileFlags(JsfFileHeader *header);
uint32_t jsfFindFile(JsfFileName name, JsfFileHeader *returnedHeader);
uint32_t jsfFindFileFromAddr(uint32_t containsAddr, JsfFileHeader *returnedHeader);
JsVar* jsvAddressToVar(size_t addr, uint32_t length);
JsVar *jsfReadFile(JsfFileName name, int offset, int length);
bool jsfWriteFile(JsfFileName name, JsVar *data, JsfFileFlags flags, JsVarInt offset, JsVarInt _size);
bool jsfEraseFile(JsfFileName name);
bool jsfEraseAll();
bool jsfCompact(bool showMessage);
JsVar *jsfListFiles(JsVar *regex, JsfFileFlags containing, JsfFileFlags notContaining);
uint32_t jsfHashFiles(JsVar *regex, JsfFileFlags containing, JsfFileFlags notContaining);
void jsfDebugFiles();
typedef enum {
  JSFSTT_QUICK,
  JSFSTT_NORMAL,
  JSFSTT_ALL,
  JSFSTT_TYPE_MASK = 7,
  JSFSTT_FIND_FILENAME_TABLE = 128,
} JsfStorageTestType;
bool jsfIsStorageValid(JsfStorageTestType testFlags);
bool jsfIsStorageEmpty();
typedef struct {
  uint32_t fileBytes;
  uint32_t fileCount;
  uint32_t trashBytes;
  uint32_t trashCount;
  uint32_t total, free;
  uint32_t firstPageWithErasedFiles;
} JsfStorageStats;
JsfStorageStats jsfGetStorageStats(uint32_t addr, bool allPages);
void jsfSaveToFlash();
void jsfLoadStateFromFlash();
void jsfSaveBootCodeToFlash(JsVar *code, bool runAfterReset);
bool jsfLoadBootCodeFromFlash(bool isReset);
JsVar *jsfGetBootCodeFromFlash(bool isReset);
bool jsfFlashContainsCode();
void jsfRemoveCodeFromFlash();
void jsfResetStorage();
JsLex *lex;
JsLex *jslSetLex(JsLex *l) {
  JsLex *old = lex;
  lex = l;
  return old;
}
void jslCharPosFree(JslCharPos *pos) {
  jsvStringIteratorFree(&pos->it);
}
void jslCharPosClone(JslCharPos *dstpos, JslCharPos *pos) {
  jsvStringIteratorClone(&dstpos->it, &pos->it);
  dstpos->currCh = pos->currCh;
}
void jslCharPosClear(JslCharPos *pos) {
  pos->it.var = 0;
}
void jslCharPosFromLex(JslCharPos *dstpos) {
  jsvStringIteratorClone(&dstpos->it, &lex->it);
  dstpos->currCh = lex->currCh;
}
void jslCharPosNew(JslCharPos *dstpos, JsVar *src, size_t tokenStart) {
  jsvStringIteratorNew(&dstpos->it, src, tokenStart);
  dstpos->currCh = jsvStringIteratorGetCharAndNext(&dstpos->it);
}
static char jslNextCh() {
  do { if (!(lex->it.ptr || lex->it.charIdx==0)) jsAssertFail("bin/espruino_embedded.c",60,""); } while(0);
  return (char)(lex->it.ptr ? (*(uint8_t*)(&lex->it.ptr[lex->it.charIdx])) : 0);
}
static void __attribute__ ((noinline)) jslGetNextCh() {
  lex->currCh = jslNextCh();
  lex->it.charIdx++;
  if (lex->it.charIdx >= lex->it.charsInVar) {
    lex->it.charIdx -= lex->it.charsInVar;
    lex->it.varIndex += lex->it.charsInVar;
    if (lex->it.var && jsvGetLastChild(lex->it.var)) {
      lex->it.var = _jsvGetAddressOf(jsvGetLastChild(lex->it.var));
      lex->it.ptr = &lex->it.var->varData.str[0];
      lex->it.charsInVar = jsvGetCharactersInVar(lex->it.var);
    } else {
      lex->it.var = 0;
      lex->it.ptr = 0;
      lex->it.charsInVar = 0;
      lex->it.varIndex += lex->it.charIdx;
      lex->it.charIdx = 0;
    }
  }
}
static void jslTokenAppendChar(char ch) {
  if (lex->tokenl < 64 -1) {
    lex->token[lex->tokenl++] = ch;
  }
}
static bool jslCheckToken(const char *token, short int tokenId) {
  int i;
  token--;
  for (i=1;i<lex->tokenl;i++) {
    if (lex->token[i]!=token[i]) return false;
  }
  if (token[lex->tokenl] == 0) {
    lex->tk = tokenId;
    return true;
  }
  return false;
}
typedef enum {
  JSLJT_SINGLE_CHAR,
  JSLJT_MAYBE_WHITESPACE,
  JSLJT_ID,
  JSLJT_NUMBER,
  JSLJT_STRING,
  JSJLT_QUESTION,
  JSLJT_EXCLAMATION,
  JSLJT_PLUS,
  JSLJT_MINUS,
  JSLJT_AND,
  JSLJT_OR,
  JSLJT_PERCENT,
  JSLJT_STAR,
  JSLJT_TOPHAT,
  JSLJT_FORWARDSLASH,
  JSLJT_LESSTHAN,
  JSLJT_EQUAL,
  JSLJT_GREATERTHAN,
} __attribute__ ((__packed__)) jslJumpTableEnum;
const jslJumpTableEnum jslJumpTable[124 +2] = {
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_MAYBE_WHITESPACE,
    JSLJT_MAYBE_WHITESPACE,
    JSLJT_MAYBE_WHITESPACE,
    JSLJT_MAYBE_WHITESPACE,
    JSLJT_MAYBE_WHITESPACE,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_MAYBE_WHITESPACE,
    JSLJT_EXCLAMATION,
    JSLJT_STRING,
    JSLJT_SINGLE_CHAR,
    JSLJT_ID,
    JSLJT_PERCENT,
    JSLJT_AND,
    JSLJT_STRING,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_STAR,
    JSLJT_PLUS,
    JSLJT_SINGLE_CHAR,
    JSLJT_MINUS,
    JSLJT_NUMBER,
    JSLJT_MAYBE_WHITESPACE,
    JSLJT_NUMBER,
    JSLJT_NUMBER,
    JSLJT_NUMBER,
    JSLJT_NUMBER,
    JSLJT_NUMBER,
    JSLJT_NUMBER,
    JSLJT_NUMBER,
    JSLJT_NUMBER,
    JSLJT_NUMBER,
    JSLJT_NUMBER,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_LESSTHAN,
    JSLJT_EQUAL,
    JSLJT_GREATERTHAN,
    JSJLT_QUESTION,
    JSLJT_SINGLE_CHAR,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_SINGLE_CHAR,
    JSLJT_TOPHAT,
    JSLJT_ID,
    JSLJT_STRING,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_ID,
    JSLJT_SINGLE_CHAR,
    JSLJT_OR,
    JSLJT_FORWARDSLASH,
};
static void jslSingleChar() {
  lex->tk = (unsigned char)lex->currCh;
  jslGetNextCh();
}
static void jslLexString() {
  char delim = lex->currCh;
  JsvStringIterator it;
  it.var = 0;
  if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
    lex->tokenValue = jsvNewFromEmptyString();
    if (!lex->tokenValue) {
      lex->tk = LEX_EOF;
      return;
    }
    jsvStringIteratorNew(&it, lex->tokenValue, 0);
  }
  jslGetNextCh();
  char lastCh = delim;
  int nesting = 0;
  bool tempatedStringHasTemplate = false;
  while (lex->currCh && (lex->currCh!=delim || nesting)) {
    if (delim=='`') {
      if ((lastCh=='$' || nesting) && lex->currCh=='{') {
        nesting++;
        tempatedStringHasTemplate = true;
      } else if (nesting && lex->currCh=='}') nesting--;
    }
    if (lex->currCh == '\\') {
      jslGetNextCh();
      char ch = lex->currCh;
      switch (lex->currCh) {
      case 'n' : ch = 0x0A; jslGetNextCh(); break;
      case 'b' : ch = 0x08; jslGetNextCh(); break;
      case 'f' : ch = 0x0C; jslGetNextCh(); break;
      case 'r' : ch = 0x0D; jslGetNextCh(); break;
      case 't' : ch = 0x09; jslGetNextCh(); break;
      case 'v' : ch = 0x0B; jslGetNextCh(); break;
      case 'u' :
      case 'x' : {
        char buf[5];
        bool isUTF8 = lex->currCh=='u';
        jslGetNextCh();
        unsigned int len = isUTF8?4:2, n=0;
        while (len--) {
          if (!lex->currCh || !isHexadecimal(lex->currCh)) {
            jsExceptionHere(JSET_ERROR, "Invalid escape sequence");
            break;
          }
          buf[n++] = lex->currCh;
          jslGetNextCh();
        }
        buf[n] = 0;
        int codepoint = (int)stringToIntWithRadix(buf,16,NULL,NULL);
        {
          ch = (char)codepoint;
        }
      } break;
      default:
        if (lex->currCh>='0' && lex->currCh<='7') {
          char buf[5] = "0";
          buf[1] = lex->currCh;
          int n=2;
          jslGetNextCh();
          if (lex->currCh>='0' && lex->currCh<='7') {
            buf[n++] = lex->currCh; jslGetNextCh();
            if (lex->currCh>='0' && lex->currCh<='7') {
              buf[n++] = lex->currCh; jslGetNextCh();
            }
          }
          buf[n]=0;
          ch = (char)stringToInt(buf);
        } else {
          jslGetNextCh();
        }
        break;
      }
      lastCh = ch;
      jsvStringIteratorAppend(&it, ch);
    } else if (lex->currCh=='\n' && delim!='`') {
      break;
    } else {
      {
        jsvStringIteratorAppend(&it, lex->currCh);
        lastCh = lex->currCh;
        jslGetNextCh();
      }
    }
  }
  jsvStringIteratorFree(&it);
  if (delim=='`' && tempatedStringHasTemplate)
    lex->tk = LEX_TEMPLATE_LITERAL;
  else lex->tk = LEX_STR;
  if (lex->currCh!=delim)
    lex->tk = (delim=='`') ? LEX_UNFINISHED_TEMPLATE_LITERAL : LEX_UNFINISHED_STR;
  jslGetNextCh();
}
static void jslLexRegex() {
  JsvStringIterator it;
  it.var = 0;
  if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
    lex->tokenValue = jsvNewFromEmptyString();
    if (!lex->tokenValue) {
      lex->tk = LEX_EOF;
      return;
    }
    jsvStringIteratorNew(&it, lex->tokenValue, 0);
    jsvStringIteratorAppend(&it, '/');
  }
  jslGetNextCh();
  while (lex->currCh && lex->currCh!='/') {
    if (lex->currCh == '\\') {
      jsvStringIteratorAppend(&it, lex->currCh);
      jslGetNextCh();
    } else if (lex->currCh=='\n') {
      break;
    }
    jsvStringIteratorAppend(&it, lex->currCh);
    jslGetNextCh();
  }
  lex->tk = LEX_REGEX;
  if (lex->currCh!='/') {
    lex->tk++;
  } else {
    jsvStringIteratorAppend(&it, '/');
    jslGetNextCh();
    while (lex->currCh=='g' ||
        lex->currCh=='i' ||
        lex->currCh=='m' ||
        lex->currCh=='y' ||
        lex->currCh=='u') {
      jsvStringIteratorAppend(&it, lex->currCh);
      jslGetNextCh();
    }
  }
  jsvStringIteratorFree(&it);
}
void jslSkipWhiteSpace() {
  jslSkipWhiteSpace_start:
  while (isWhitespaceInline(lex->currCh))
    jslGetNextCh();
  if (lex->currCh=='/') {
    if (jslNextCh()=='/') {
      while (lex->currCh && lex->currCh!='\n') jslGetNextCh();
      jslGetNextCh();
      goto jslSkipWhiteSpace_start;
    }
    if (jslNextCh()=='*') {
      jslGetNextCh();
      jslGetNextCh();
      while (lex->currCh && !(lex->currCh=='*' && jslNextCh()=='/'))
        jslGetNextCh();
      if (!lex->currCh) {
        lex->tk = LEX_UNFINISHED_COMMENT;
        return;
      }
      jslGetNextCh();
      jslGetNextCh();
      goto jslSkipWhiteSpace_start;
    }
  }
}
static void jslGetRawString() {
  do { if (!(lex->tk == LEX_RAW_STRING8 || lex->tk == LEX_RAW_STRING16)) jsAssertFail("bin/espruino_embedded.c",590,""); } while(0);
  bool is16Bit = lex->tk == LEX_RAW_STRING16;
  lex->tk = LEX_STR;
  size_t length = (unsigned char)lex->currCh;
  if (is16Bit) {
    jslGetNextCh();
    length |= ((unsigned char)lex->currCh)<<8;
  }
  jsvUnLock(lex->tokenValue);
  if (length > (4 + ((14*3 + 0)>>3))) {
    size_t stringPos = jsvStringIteratorGetIndex(&lex->it);
    lex->tokenValue = jsvNewFromStringVar(lex->sourceVar, stringPos, length);
    jsvLockAgain(lex->it.var);
    jsvStringIteratorGoto(&lex->it, lex->sourceVar, stringPos+length);
    jsvUnLock(lex->it.var);
  } else {
    lex->tokenValue = jsvNewWithFlags(JSV_STRING_0 + length);
    for (size_t i=0;i<length;i++) {
      jslGetNextCh();
      lex->tokenValue->varData.str[i] = lex->currCh;
    }
  }
  jslGetNextCh();
}
void jslGetNextToken() {
  int lastToken = lex->tk;
  lex->tk = LEX_EOF;
  lex->tokenl = 0;
  if (lex->tokenValue) {
    jsvUnLock(lex->tokenValue);
    lex->tokenValue = 0;
  }
  lex->tokenLastStart = lex->tokenStart;
  unsigned char jumpCh = (unsigned char)lex->currCh;
  if (jumpCh > 124) jumpCh = 0;
  jslGetNextToken_start:
  lex->tokenStart = jsvStringIteratorGetIndex(&lex->it) - 1;
  switch(jslJumpTable[jumpCh]) {
    case JSLJT_MAYBE_WHITESPACE:
      jslSkipWhiteSpace();
      jumpCh = (unsigned char)lex->currCh;
      if (jumpCh > 124) jumpCh = 0;
      if (jumpCh=='/') jumpCh = (124 +1);
      goto jslGetNextToken_start;
      break;
    case JSLJT_SINGLE_CHAR:
      jslSingleChar();
      if (lex->tk == LEX_R_THIS) lex->hadThisKeyword=true;
      else if (lex->tk>=LEX_RAW_STRING8) {
        if (lex->tk == LEX_RAW_STRING8 || lex->tk == LEX_RAW_STRING16) jslGetRawString();
        else if (lex->tk == LEX_RAW_INT0) {
          lex->tk = LEX_INT;
          lex->tokenValue = jsvNewFromInteger(0);
        } else if (lex->tk == LEX_RAW_INT8) {
          lex->tk = LEX_INT;
          lex->tokenValue = jsvNewFromInteger((int8_t)lex->currCh);
          jslGetNextCh();
        } else if (lex->tk == LEX_RAW_INT16) {
          lex->tk = LEX_INT;
          int16_t value = (unsigned char)lex->currCh;
          jslGetNextCh();
          value |= ((char)lex->currCh)<<8;
          jslGetNextCh();
          lex->tokenValue = jsvNewFromInteger(value);
        }
      }
      break;
    case JSLJT_ID: {
      while (isAlphaInline(lex->currCh) || isNumericInline(lex->currCh) || lex->currCh=='$') {
        jslTokenAppendChar(lex->currCh);
        jslGetNextCh();
      }
      lex->tk = LEX_ID;
      if (!lex->token[1]) break;
      switch (lex->token[0]) {
      case 'b': jslCheckToken("reak", LEX_R_BREAK);
      break;
      case 'c': if (!jslCheckToken("ase", LEX_R_CASE))
                if (!jslCheckToken("atch", LEX_R_CATCH))
                if (!jslCheckToken("lass", LEX_R_CLASS))
                if (!jslCheckToken("onst", LEX_R_CONST))
                jslCheckToken("ontinue", LEX_R_CONTINUE);
      break;
      case 'd': if (!jslCheckToken("efault", LEX_R_DEFAULT))
                if (!jslCheckToken("elete", LEX_R_DELETE))
                if (!jslCheckToken("o", LEX_R_DO))
                jslCheckToken("ebugger", LEX_R_DEBUGGER);
      break;
      case 'e': if (!jslCheckToken("lse", LEX_R_ELSE))
                jslCheckToken("xtends", LEX_R_EXTENDS);
      break;
      case 'f': if (!jslCheckToken("alse", LEX_R_FALSE))
                if (!jslCheckToken("inally", LEX_R_FINALLY))
                if (!jslCheckToken("or", LEX_R_FOR))
                jslCheckToken("unction", LEX_R_FUNCTION);
      break;
      case 'i': if (!jslCheckToken("f", LEX_R_IF))
                if (!jslCheckToken("n", LEX_R_IN))
                jslCheckToken("nstanceof", LEX_R_INSTANCEOF);
      break;
      case 'l': jslCheckToken("et", LEX_R_LET);
      break;
      case 'n': if (!jslCheckToken("ew", LEX_R_NEW))
                jslCheckToken("ull", LEX_R_NULL);
      break;
      case 'o': jslCheckToken("f", LEX_R_OF);
      break;
      case 'r': jslCheckToken("eturn", LEX_R_RETURN);
      break;
      case 's': if (!jslCheckToken("tatic", LEX_R_STATIC))
                if (!jslCheckToken("uper", LEX_R_SUPER))
                jslCheckToken("witch", LEX_R_SWITCH);
      break;
      case 't': if (jslCheckToken("his", LEX_R_THIS)) lex->hadThisKeyword=true;
                else if (!jslCheckToken("hrow", LEX_R_THROW))
                if (!jslCheckToken("rue", LEX_R_TRUE))
                if (!jslCheckToken("ry", LEX_R_TRY))
                     jslCheckToken("ypeof", LEX_R_TYPEOF);
      break;
      case 'u': jslCheckToken("ndefined", LEX_R_UNDEFINED);
      break;
      case 'w': jslCheckToken("hile",LEX_R_WHILE);
      break;
      case 'v': if (!jslCheckToken("ar",LEX_R_VAR))
                jslCheckToken("oid",LEX_R_VOID);
      break;
      default: break;
      } break;
      case JSLJT_NUMBER: {
        bool canBeFloating = true;
        if (lex->currCh=='.') {
          jslGetNextCh();
          if (isNumericInline(lex->currCh)) {
            lex->tk = LEX_FLOAT;
            jslTokenAppendChar('.');
          } else {
            lex->tk = '.';
            break;
          }
        } else {
          if (lex->currCh=='0') {
            jslTokenAppendChar(lex->currCh);
            jslGetNextCh();
            if ((lex->currCh=='x' || lex->currCh=='X') ||
                (lex->currCh=='b' || lex->currCh=='B') ||
                (lex->currCh=='o' || lex->currCh=='O')) {
              canBeFloating = false;
              jslTokenAppendChar(lex->currCh); jslGetNextCh();
            }
          }
          lex->tk = LEX_INT;
          while (isNumericInline(lex->currCh) || (!canBeFloating && isHexadecimal(lex->currCh)) || lex->currCh=='_') {
            if (lex->currCh != '_') jslTokenAppendChar(lex->currCh);
            jslGetNextCh();
          }
          if (canBeFloating && lex->currCh=='.') {
            lex->tk = LEX_FLOAT;
            jslTokenAppendChar('.');
            jslGetNextCh();
          }
        }
        if (lex->tk == LEX_FLOAT) {
          while (isNumeric(lex->currCh) || lex->currCh=='_') {
            if (lex->currCh != '_') jslTokenAppendChar(lex->currCh);
            jslGetNextCh();
          }
        }
        if (canBeFloating && (lex->currCh=='e'||lex->currCh=='E')) {
          lex->tk = LEX_FLOAT;
          jslTokenAppendChar(lex->currCh);
          jslGetNextCh();
          if (lex->currCh=='-' || lex->currCh=='+')
          {
            jslTokenAppendChar(lex->currCh);
            jslGetNextCh();
          }
          while (isNumeric(lex->currCh) || lex->currCh=='_') {
            if (lex->currCh != '_') jslTokenAppendChar(lex->currCh);
            jslGetNextCh();
          }
        }
      } break;
      case JSLJT_STRING: jslLexString(); break;
      case JSLJT_EXCLAMATION: jslSingleChar();
      if (lex->currCh=='=') {
        lex->tk = LEX_NEQUAL;
        jslGetNextCh();
        if (lex->currCh=='=') {
          lex->tk = LEX_NTYPEEQUAL;
          jslGetNextCh();
        }
      } break;
      case JSLJT_PLUS: jslSingleChar();
      if (lex->currCh=='=') {
        lex->tk = LEX_PLUSEQUAL;
        jslGetNextCh();
      } else if (lex->currCh=='+') {
        lex->tk = LEX_PLUSPLUS;
        jslGetNextCh();
      } break;
      case JSLJT_MINUS: jslSingleChar();
      if (lex->currCh=='=') {
        lex->tk = LEX_MINUSEQUAL;
        jslGetNextCh();
      } else if (lex->currCh=='-') {
        lex->tk = LEX_MINUSMINUS;
        jslGetNextCh();
      } break;
      case JSLJT_AND: jslSingleChar();
      if (lex->currCh=='=') {
        lex->tk = LEX_ANDEQUAL;
        jslGetNextCh();
      } else if (lex->currCh=='&') {
        lex->tk = LEX_ANDAND;
        jslGetNextCh();
      } break;
      case JSLJT_OR: jslSingleChar();
      if (lex->currCh=='=') {
        lex->tk = LEX_OREQUAL;
        jslGetNextCh();
      } else if (lex->currCh=='|') {
        lex->tk = LEX_OROR;
        jslGetNextCh();
      } break;
      case JSLJT_TOPHAT: jslSingleChar();
      if (lex->currCh=='=') {
        lex->tk = LEX_XOREQUAL;
        jslGetNextCh();
      } break;
      case JSLJT_STAR: jslSingleChar();
      if (lex->currCh=='=') {
        lex->tk = LEX_MULEQUAL;
        jslGetNextCh();
      } break;
      case JSJLT_QUESTION: jslSingleChar();
      if(lex->currCh=='?'){
        lex->tk = LEX_NULLISH;
        jslGetNextCh();
      } break;
      case JSLJT_FORWARDSLASH:
      if (lastToken==LEX_EOF ||
          (lastToken>=_LEX_TOKENS_START && lastToken<=_LEX_TOKENS_END &&
           lastToken!=LEX_R_TRUE && lastToken!=LEX_R_FALSE && lastToken!=LEX_R_NULL && lastToken!=LEX_R_UNDEFINED) ||
          lastToken=='!' ||
          lastToken=='%' ||
          lastToken=='&' ||
          lastToken=='*' ||
          lastToken=='+' ||
          lastToken=='-' ||
          lastToken=='/' ||
          lastToken=='<' ||
          lastToken=='=' ||
          lastToken=='>' ||
          lastToken=='?' ||
          lastToken=='[' ||
          lastToken=='{' ||
          lastToken=='}' ||
          lastToken=='(' ||
          lastToken==',' ||
          lastToken==';' ||
          lastToken==':') {
        jslLexRegex();
      } else {
        jslSingleChar();
        if (lex->currCh=='=') {
          lex->tk = LEX_DIVEQUAL;
          jslGetNextCh();
        }
      } break;
      case JSLJT_PERCENT: jslSingleChar();
      if (lex->currCh=='=') {
        lex->tk = LEX_MODEQUAL;
        jslGetNextCh();
      } break;
      case JSLJT_EQUAL: jslSingleChar();
      if (lex->currCh=='=') {
        lex->tk = LEX_EQUAL;
        jslGetNextCh();
        if (lex->currCh=='=') {
          lex->tk = LEX_TYPEEQUAL;
          jslGetNextCh();
        }
      } else if (lex->currCh=='>') {
        lex->tk = LEX_ARROW_FUNCTION;
        jslGetNextCh();
      } break;
      case JSLJT_LESSTHAN: jslSingleChar();
      if (lex->currCh=='=') {
        lex->tk = LEX_LEQUAL;
        jslGetNextCh();
      } else if (lex->currCh=='<') {
        lex->tk = LEX_LSHIFT;
        jslGetNextCh();
        if (lex->currCh=='=') {
          lex->tk = LEX_LSHIFTEQUAL;
          jslGetNextCh();
        }
      } break;
      case JSLJT_GREATERTHAN: jslSingleChar();
      if (lex->currCh=='=') {
        lex->tk = LEX_GEQUAL;
        jslGetNextCh();
      } else if (lex->currCh=='>') {
        lex->tk = LEX_RSHIFT;
        jslGetNextCh();
        if (lex->currCh=='=') {
          lex->tk = LEX_RSHIFTEQUAL;
          jslGetNextCh();
        } else if (lex->currCh=='>') {
          jslGetNextCh();
          if (lex->currCh=='=') {
            lex->tk = LEX_RSHIFTUNSIGNEDEQUAL;
            jslGetNextCh();
          } else {
            lex->tk = LEX_RSHIFTUNSIGNED;
          }
        }
      } break;
      default: do { if (!(0)) jsAssertFail("bin/espruino_embedded.c",929,""); } while(0);break;
    }
  }
}
static void jslPreload() {
  jslGetNextCh();
  jslGetNextToken();
}
void jslInit(JsVar *var) {
  lex->sourceVar = jsvLockAgain(var);
  lex->tk = 0;
  lex->tokenStart = 0;
  lex->tokenLastStart = 0;
  lex->tokenl = 0;
  lex->tokenValue = 0;
  lex->functionName = NULL;
  lex->lastLex = NULL;
  jsvStringIteratorNew(&lex->it, lex->sourceVar, 0);
  jsvUnLock(lex->it.var);
  jslPreload();
}
void jslKill() {
  lex->tk = LEX_EOF;
  if (lex->it.var) jsvLockAgain(lex->it.var);
  jsvStringIteratorFree(&lex->it);
  if (lex->tokenValue) {
    jsvUnLock(lex->tokenValue);
    lex->tokenValue = 0;
  }
  jsvUnLock(lex->sourceVar);
}
void jslSeekTo(size_t seekToChar) {
  if (lex->it.var) jsvLockAgain(lex->it.var);
  jsvStringIteratorFree(&lex->it);
  jsvStringIteratorNew(&lex->it, lex->sourceVar, seekToChar);
  jsvUnLock(lex->it.var);
  lex->tokenStart = 0;
  lex->tokenLastStart = 0;
  lex->tk = LEX_EOF;
  jslPreload();
}
void jslSeekToP(JslCharPos *seekToChar) {
  if (lex->it.var) jsvLockAgain(lex->it.var);
  jsvStringIteratorFree(&lex->it);
  jsvStringIteratorClone(&lex->it, &seekToChar->it);
  jsvUnLock(lex->it.var);
  lex->currCh = seekToChar->currCh;
  lex->tokenStart = 0;
  lex->tokenLastStart = 0;
  lex->tk = LEX_EOF;
  jslGetNextToken();
}
void jslReset() {
  jslSeekTo(0);
}
void jslFunctionCharAsString(unsigned char ch, char *str, size_t len) {
  if (ch >= LEX_TOKEN_START) {
    jslTokenAsString(ch, str, len);
  } else {
    str[0] = (char)ch;
    str[1] = 0;
  }
}
const char* jslReservedWordAsString(int token) {
  static const char tokenNames[] =
                               "==\0"
                               "===\0"
                               "!=\0"
                               "!==\0"
                               "<=\0"
                               "<<\0"
                               "<<=\0"
                               ">=\0"
                               ">>\0"
                               ">>>\0"
                               ">>=\0"
                                    ">>>=\0"
                               "+=\0"
                               "-=\0"
                               "++\0"
                               "--\0"
                               "*=\0"
                               "/=\0"
                               "%=\0"
                               "&=\0"
                               "&&\0"
                               "|=\0"
                               "||\0"
                               "^=\0"
                               "=>\0"
                            "if\0"
                            "else\0"
                            "do\0"
                            "while\0"
                            "for\0"
                            "break\0"
                            "continue\0"
                            "function\0"
                            "return\0"
                            "var\0"
                            "let\0"
                            "const\0"
                            "this\0"
                            "throw\0"
                            "try\0"
                            "catch\0"
                            "finally\0"
                            "true\0"
                            "false\0"
                            "null\0"
                            "undefined\0"
                            "new\0"
                            "in\0"
                            "instanceof\0"
                            "switch\0"
                            "case\0"
                            "default\0"
                            "delete\0"
                            "typeof\0"
                            "void\0"
                            "debugger\0"
                            "class\0"
                            "extends\0"
                            "super\0"
                            "static\0"
                            "of\0"
                                                           "\0\0\0\0\0\0\0\0\0"
                            "??\0"
      ;
  unsigned int p = 0;
  int n = token-_LEX_TOKENS_START;
  while (n>0 && p<sizeof(tokenNames)) {
    while (tokenNames[p] && p<sizeof(tokenNames)) p++;
    p++;
    n--;
  }
  do { if (!(n==0)) jsAssertFail("bin/espruino_embedded.c",1086,""); } while(0);
  return &tokenNames[p];
}
void jslTokenAsString(int token, char *str, size_t len) {
  do { if (!(len>28)) jsAssertFail("bin/espruino_embedded.c",1091,""); } while(0);
  if (token>32 && token<128) {
    do { if (!(len>=4)) jsAssertFail("bin/espruino_embedded.c",1094,""); } while(0);
    str[0] = '\'';
    str[1] = (char)token;
    str[2] = '\'';
    str[3] = 0;
    return;
  }
  switch (token) {
  case LEX_EOF : strcpy(str, "EOF"); return;
  case LEX_ID : strcpy(str, "ID"); return;
  case LEX_INT : strcpy(str, "INT"); return;
  case LEX_FLOAT : strcpy(str, "FLOAT"); return;
  case LEX_STR : strcpy(str, "STRING"); return;
  case LEX_UNFINISHED_STR : strcpy(str, "UNFINISHED STRING"); return;
  case LEX_TEMPLATE_LITERAL : strcpy(str, "TEMPLATE LITERAL"); return;
  case LEX_UNFINISHED_TEMPLATE_LITERAL : strcpy(str, "UNFINISHED TEMPLATE LITERAL"); return;
  case LEX_REGEX : strcpy(str, "REGEX"); return;
  case LEX_UNFINISHED_REGEX : strcpy(str, "UNFINISHED REGEX"); return;
  case LEX_UNFINISHED_COMMENT : strcpy(str, "UNFINISHED COMMENT"); return;
  case 255 : strcpy(str, "[ERASED]"); return;
  }
  if (token>=_LEX_TOKENS_START && token<=_LEX_TOKENS_END) {
    strcpy(str, jslReservedWordAsString(token));
    return;
  }
  espruino_snprintf(str, len, "?[%d]", token);
}
void jslGetTokenString(char *str, size_t len) {
  if (lex->tk == LEX_ID) {
    espruino_snprintf(str, len, "ID:%s", jslGetTokenValueAsString());
  } else if (lex->tk == LEX_STR) {
    espruino_snprintf(str, len, "String:'%s'", jslGetTokenValueAsString());
  } else
    jslTokenAsString(lex->tk, str, len);
}
char *jslGetTokenValueAsString() {
  if (lex->tokenl==0 && lex->tokenValue)
    lex->tokenl = (unsigned char)jsvGetString(lex->tokenValue, lex->token, 64);
  do { if (!(lex->tokenl < 64)) jsAssertFail("bin/espruino_embedded.c",1137,""); } while(0);
  lex->token[lex->tokenl] = 0;
  if (lex->tokenl==0 && (lex->tk >= _LEX_R_LIST_START && lex->tk <= _LEX_R_LIST_END)) {
    jslTokenAsString(lex->tk, lex->token, sizeof(lex->token));
    strcpy(lex->token, jslReservedWordAsString(lex->tk));
    lex->tokenl = (unsigned char)strlen(lex->token);
  }
  return lex->token;
}
size_t jslGetTokenLength() {
  if (lex->tokenValue)
    return jsvGetStringLength(lex->tokenValue);
  return (size_t)lex->tokenl;
}
JsVar *jslGetTokenValueAsVar() {
  if (lex->tokenValue) {
    return jsvLockAgain(lex->tokenValue);
  } else if (lex->tk == LEX_INT) {
    return jsvNewFromLongInteger(stringToInt(jslGetTokenValueAsString()));
  } else if ((lex->tk >= _LEX_R_LIST_START && lex->tk <= _LEX_R_LIST_END)) {
    return jsvNewFromString(jslReservedWordAsString(lex->tk));
  } else {
    do { if (!(lex->tokenl < 64)) jsAssertFail("bin/espruino_embedded.c",1164,""); } while(0);
    lex->token[lex->tokenl] = 0;
    return jsvNewFromString(lex->token);
  }
}
bool jslIsIDOrReservedWord() {
  return lex->tk == LEX_ID ||
         (lex->tk >= _LEX_R_LIST_START && lex->tk <= _LEX_R_LIST_END);
}
static void jslMatchError(int expected_tk) {
  char gotStr[30];
  char expStr[30];
  jslGetTokenString(gotStr, sizeof(gotStr));
  jslTokenAsString(expected_tk, expStr, sizeof(expStr));
  size_t oldPos = lex->tokenLastStart;
  lex->tokenLastStart = lex->tokenStart;
  jsExceptionHere(JSET_SYNTAXERROR, "Got %s expected %s", gotStr, expStr);
  lex->tokenLastStart = oldPos;
  jslGetNextToken();
}
bool jslMatch(int expected_tk) {
  if (lex->tk != expected_tk) {
    jslMatchError(expected_tk);
    return false;
  }
  jslGetNextToken();
  return true;
}
static bool jslPreserveSpaceBetweenTokens(int lastTk, int newTk) {
  if ((lastTk==LEX_ID || lastTk==LEX_FLOAT || lastTk==LEX_INT) &&
      (newTk==LEX_ID || newTk==LEX_FLOAT || newTk==LEX_INT)) return true;
  if ((lastTk=='-' && newTk=='-') ||
      (lastTk=='+' && newTk=='+') ||
      (lastTk=='/' && newTk==LEX_REGEX) ||
      (lastTk==LEX_REGEX && (newTk=='/' || newTk==LEX_ID)))
    return true;
  return false;
}
static void _jslNewTokenisedStringFromLexerCopyString(size_t *length, JsvStringIterator *dstit, JsvStringIterator *it, char itch) {
  *length += jsvStringIteratorGetIndex(&lex->it)-(lex->tokenStart+1);
  if (dstit) {
    jsvStringIteratorSetCharAndNext(dstit, itch);
    while (jsvStringIteratorGetIndex(it)+1 < jsvStringIteratorGetIndex(&lex->it)) {
      jsvStringIteratorSetCharAndNext(dstit, jsvStringIteratorGetCharAndNext(it));
    }
  }
}
static size_t _jslNewTokenisedStringFromLexer(JsvStringIterator *dstit, JsVar *dstVar, JslCharPos *charFrom, size_t charTo) {
  jslSeekToP(charFrom);
  JsvStringIterator it;
  char itch = charFrom->currCh;
  if (dstit)
    jsvStringIteratorClone(&it, &charFrom->it);
  size_t length = 0;
  int lastTk = LEX_EOF;
  int atobChecker = 0;
  while (lex->tk!=LEX_EOF && jsvStringIteratorGetIndex(&lex->it)<=charTo+1) {
    if (jslPreserveSpaceBetweenTokens(lastTk, lex->tk)) {
      length++;
      if (dstit) jsvStringIteratorSetCharAndNext(dstit, ' ');
    }
    size_t l;
    if (lex->tk==LEX_STR && ((l = jslGetTokenLength())!=0)
                       ) {
      JsVar *v = NULL;
      jslSkipWhiteSpace();
      if (atobChecker==2 && lex->currCh==')') {
        JsVar *b64 = jslGetTokenValueAsVar();
        v = jswrap_atob(b64);
        jsvUnLock(b64);
        l = jsvGetStringLength(v);
        length -= 5;
        if (dstit) jsvStringIteratorGoto(dstit, dstVar, length);
        itch = lex->currCh;
        jslGetNextToken();
      }
      atobChecker = 0;
      if (dstit) {
        jsvStringIteratorSetCharAndNext(dstit, (char)((l<256) ? LEX_RAW_STRING8 : LEX_RAW_STRING16));
        jsvStringIteratorSetCharAndNext(dstit, (char)(l&255));
        if (l>=256) jsvStringIteratorSetCharAndNext(dstit, (char)(l>>8));
        if (!v) v = jslGetTokenValueAsVar();
        JsvStringIterator sit;
        jsvStringIteratorNew(&sit, v, 0);
        while (jsvStringIteratorHasChar(&sit)) {
          jsvStringIteratorSetCharAndNext(dstit, jsvStringIteratorGetCharAndNext(&sit));
        }
        jsvStringIteratorFree(&sit);
      }
      jsvUnLock(v);
      length += ((l<256)?2:3) + l;
    } else if (lex->tk==LEX_INT) {
      long long v = jsvGetLongIntegerAndUnLock(jslGetTokenValueAsVar());
      if (v==0) {
        length++;
        if (dstit) jsvStringIteratorSetCharAndNext(dstit, (char)LEX_RAW_INT0);
      } else if (v>=-128 && v<128) {
        length += 2;
        if (dstit) {
          jsvStringIteratorSetCharAndNext(dstit, (char)LEX_RAW_INT8);
          jsvStringIteratorSetCharAndNext(dstit, (char)v);
        }
      } else if (v>=-32768 && v<32768) {
        length += 3;
        if (dstit) {
          jsvStringIteratorSetCharAndNext(dstit, (char)LEX_RAW_INT16);
          jsvStringIteratorSetCharAndNext(dstit, (char)(v&255));
          jsvStringIteratorSetCharAndNext(dstit, (char)(v>>8));
        }
      } else
        _jslNewTokenisedStringFromLexerCopyString(&length, dstit, &it, itch);
    } else if (lex->tk==LEX_ID ||
        lex->tk==LEX_INT ||
        lex->tk==LEX_FLOAT ||
        lex->tk==LEX_STR ||
        lex->tk==LEX_TEMPLATE_LITERAL ||
        lex->tk==LEX_REGEX) {
      if (lex->tk==LEX_ID && strcmp(jslGetTokenValueAsString(),"atob")==0)
        atobChecker = 1;
      else
        atobChecker = 0;
      _jslNewTokenisedStringFromLexerCopyString(&length, dstit, &it, itch);
    } else {
      if (atobChecker==1 && lex->tk=='(')
        atobChecker = 2;
      else
        atobChecker = 0;
      if (dstit)
        jsvStringIteratorSetCharAndNext(dstit, (char)lex->tk);
      length++;
    }
    lastTk = lex->tk;
    jslSkipWhiteSpace();
    if (dstit) {
      jsvStringIteratorFree(&it);
      jsvStringIteratorClone(&it, &lex->it);
    }
    itch = lex->currCh;
    jslGetNextToken();
  }
  if (dstit)
    jsvStringIteratorFree(&it);
  return length;
}
JsVar *jslNewTokenisedStringFromLexer(JslCharPos *charFrom, size_t charTo) {
  JsLex *oldLex = lex;
  JsLex newLex;
  lex = &newLex;
  jslInit(oldLex->sourceVar);
  size_t length = _jslNewTokenisedStringFromLexer(NULL, NULL, charFrom, charTo);
  JsVar *var = jsvNewStringOfLength((unsigned int)length, NULL);
  if (var) {
    JsvStringIterator dstit;
    jsvStringIteratorNew(&dstit, var, 0);
    _jslNewTokenisedStringFromLexer(&dstit, var, charFrom, charTo);
    jsvStringIteratorFree(&dstit);
  }
  jslKill();
  lex = oldLex;
  return var;
}
JsVar *jslNewStringFromLexer(JslCharPos *charFrom, size_t charTo) {
  size_t maxLength = charTo + 1 - jsvStringIteratorGetIndex(&charFrom->it);
  do { if (!(maxLength>0)) jsAssertFail("bin/espruino_embedded.c",1364,""); } while(0);
  JsVar *var = 0;
  if (maxLength > ((4 + ((14*3 + 0)>>3)) + (4 + ((14*3 + 0 + 8)>>3)))) {
    var = jsvNewFlatStringOfLength((unsigned int)maxLength);
    if (var) {
      char *flatPtr = jsvGetFlatStringPointer(var);
      *(flatPtr++) = charFrom->currCh;
      JsvStringIterator it;
      jsvStringIteratorClone(&it, &charFrom->it);
      while (jsvStringIteratorHasChar(&it) && (--maxLength>0)) {
        *(flatPtr++) = jsvStringIteratorGetCharAndNext(&it);
      }
      jsvStringIteratorFree(&it);
      return var;
    }
  }
  var = jsvNewFromEmptyString();
  if (!var) {
    return 0;
  }
  JsVar *block = jsvLockAgain(var);
  block->varData.str[0] = charFrom->currCh;
  size_t blockChars = 1;
  size_t totalStringLength = maxLength;
  JsvStringIterator it;
  jsvStringIteratorClone(&it, &charFrom->it);
  while (jsvStringIteratorHasChar(&it) && (--maxLength>0)) {
    char ch = jsvStringIteratorGetCharAndNext(&it);
    if (blockChars >= jsvGetMaxCharactersInVar(block)) {
      jsvSetCharactersInVar(block, blockChars);
      JsVar *next = jsvNewWithFlags(JSV_STRING_EXT_0);
      if (!next) break;
      jsvSetLastChild(block, jsvGetRef(next));
      jsvUnLock(block);
      block = next;
      blockChars=0;
    }
    block->varData.str[blockChars++] = ch;
  }
  jsvSetCharactersInVar(block, blockChars);
  jsvUnLock(block);
  do { if (!((totalStringLength == jsvGetStringLength(var)) || (jsErrorFlags&JSERR_MEMORY) || !jsvStringIteratorHasChar(&it))) jsAssertFail("bin/espruino_embedded.c",1417,""); } while(0);
  jsvStringIteratorFree(&it);
  return var;
}
bool jslNeedSpaceBetween(unsigned char lastch, unsigned char ch) {
  return ((lastch>=_LEX_R_LIST_START && lastch<=_LEX_R_LIST_END) || (ch>=_LEX_R_LIST_START && ch<=_LEX_R_LIST_END)) &&
         (lastch>=_LEX_R_LIST_START || isAlpha((char)lastch) || isNumeric((char)lastch)) &&
         (ch>=_LEX_R_LIST_START || isAlpha((char)ch) || isNumeric((char)ch));
}
static void jslPrintTokenisedChar(JsvStringIterator *it, unsigned char *lastch, size_t *col, size_t *chars, vcbprintf_callback user_callback, void *user_data) {
  unsigned char ch = (unsigned char)jsvStringIteratorGetCharAndNext(it);
  char buf[64];
  if (ch==LEX_RAW_STRING8 || ch==LEX_RAW_STRING16) {
    size_t length = (unsigned char)jsvStringIteratorGetCharAndNext(it);
    if (ch==LEX_RAW_STRING16) {
      (*chars)++;
      length |= ((unsigned char)jsvStringIteratorGetCharAndNext(it))<<8;
    }
    (*chars)+=2;
    user_callback("\"", user_data);
    while (length--) {
      char ch = jsvStringIteratorGetCharAndNext(it);
      const char *s = escapeCharacter(ch, 0, false);
      (*chars)++;
      user_callback(s, user_data);
    }
    user_callback("\"", user_data);
    return;
  } else if (ch==LEX_RAW_INT0) {
    (*chars)++;
    user_callback("0", user_data);
    return;
  } else if (ch==LEX_RAW_INT8 || ch==LEX_RAW_INT16) {
    int16_t value = (unsigned char)jsvStringIteratorGetCharAndNext(it);
    if (ch==LEX_RAW_INT16) {
      value |= ((char)jsvStringIteratorGetCharAndNext(it))<<8;
    }
    itostr(value, buf, 10);
    (*chars)+=strlen(buf);
    user_callback(buf, user_data);
    return;
  }
  if (jslNeedSpaceBetween(*lastch, ch)) {
    (*col)++;
    user_callback(" ", user_data);
  }
  jslFunctionCharAsString(ch, buf, sizeof(buf));
  size_t len = strlen(buf);
  if (len) (*col) += len-1;
  user_callback(buf, user_data);
  (*chars)++;
  *lastch = ch;
}
void jslPrintTokenisedString(JsVar *code, vcbprintf_callback user_callback, void *user_data) {
  unsigned char lastch = 0;
  size_t col=0, chars=0;
  JsvStringIterator it;
  jsvStringIteratorNew(&it, code, 0);
  while (jsvStringIteratorHasChar(&it)) {
    jslPrintTokenisedChar(&it, &lastch, &col, &chars, user_callback, user_data);
  }
  jsvStringIteratorFree(&it);
}
void jslPrintPosition(vcbprintf_callback user_callback, void *user_data, JsLex *lex, size_t tokenPos) {
  size_t line,col,ignoredLines;
  jsvGetLineAndCol(lex->sourceVar, tokenPos, &line, &col, &ignoredLines);
  cbprintf(user_callback, user_data, ":%d:%d", line-ignoredLines, col);
}
void jslPrintTokenLineMarker(vcbprintf_callback user_callback, void *user_data, JsLex *lex, size_t tokenPos, size_t prefixLength) {
  size_t line = 1,col = 1;
  jsvGetLineAndCol(lex->sourceVar, tokenPos, &line, &col, NULL);
  size_t startOfLine = jsvGetIndexFromLineAndCol(lex->sourceVar, line, 1);
  size_t lineLength = jsvGetCharsOnLine(lex->sourceVar, line);
  if (lineLength>60 && tokenPos-startOfLine>30) {
    cbprintf(user_callback, user_data, "...");
    size_t skipChars = tokenPos-30 - startOfLine;
    startOfLine += 3+skipChars;
    if (skipChars<=col)
      col -= skipChars;
    else
      col = 0;
    lineLength -= skipChars;
  }
  size_t chars = 0;
  JsvStringIterator it;
  jsvStringIteratorNew(&it, lex->sourceVar, startOfLine);
  unsigned char lastch = 0;
  while (jsvStringIteratorHasChar(&it) && chars<60 && lastch!=255) {
    if (jsvStringIteratorGetChar(&it) == '\n') break;
    jslPrintTokenisedChar(&it, &lastch, &col, &chars, user_callback, user_data);
  }
  jsvStringIteratorFree(&it);
  if (lineLength > 60)
    user_callback("...", user_data);
  user_callback("\n", user_data);
  col += prefixLength;
  while (col-- > 1) user_callback(" ", user_data);
  user_callback("^\n", user_data);
}
void jslPrintStackTrace(vcbprintf_callback user_callback, void *user_data, JsLex *lex) {
  while (lex) {
    user_callback("    at ", user_data);
    if (lex->functionName) {
      char functionName[64];
      jsvGetString(lex->functionName, functionName, sizeof(functionName));
      user_callback(functionName, user_data);
      user_callback(" (", user_data);
    }
    jslPrintPosition(user_callback, user_data, lex, lex->tokenLastStart);
    user_callback(lex->functionName ? ")\n":"\n", user_data);
    jslPrintTokenLineMarker(user_callback, user_data, lex, lex->tokenLastStart, 0);
    lex = lex->lastLex;
  }
}
typedef enum {
  JSF_NONE,
  JSF_DEEP_SLEEP = 1<<0,
  JSF_UNSAFE_FLASH = 1<<1,
  JSF_UNSYNC_FILES = 1<<2,
  JSF_PRETOKENISE = 1<<3,
  JSF_ON_ERROR_SAVE = 1<<5,
  JSF_ON_ERROR_FLASH_LED = 1<<6,
} __attribute__ ((__packed__)) JsFlags;
extern volatile JsFlags jsFlags;
bool jsfGetFlag(JsFlags flag);
void jsfSetFlag(JsFlags flag, bool isOn);
JsVar *jsfGetFlags();
void jsfSetFlags(JsVar *flags);

volatile JsFlags jsFlags;
const char *jsFlagNames = "deepSleep\0unsafeFlash\0unsyncFiles\0pretokenise\0jitDebug\0onErrorSave\0onErrorFlash\0";
bool jsfGetFlag(JsFlags flag) {
  return (jsFlags & flag)!=0;
}
void jsfSetFlag(JsFlags flag, bool isOn) {
  if (isOn)
    jsFlags |= flag;
  else
    jsFlags &= ~flag;
}
JsVar *jsfGetFlags() {
 JsVar *o = jsvNewWithFlags(JSV_OBJECT);
 if (!o) return 0;
 const char *p = jsFlagNames;
 JsFlags flag = 1;
 while (*p) {
   jsvObjectSetChildAndUnLock(o, p, jsvNewFromInteger(jsfGetFlag(flag)?1:0));
   p += strlen(p)+1;
   flag<<=1;
 }
 return o;
}
void jsfSetFlags(JsVar *flags) {
  if (!jsvIsObject(flags)) return;
  const char *p = jsFlagNames;
  JsFlags flag = 1;
  while (*p) {
    JsVar *v = jsvObjectGetChildIfExists(flags, p);
    if (v) jsfSetFlag(flag, jsvGetBoolAndUnLock(v));
    p += strlen(p)+1;
    flag<<=1;
  }
}
typedef enum {
  JSON_NONE,
  JSON_SOME_NEWLINES = 1,
  JSON_ALL_NEWLINES = 2,
  JSON_PRETTY = 4,
  JSON_LIMIT = 8,
  JSON_IGNORE_FUNCTIONS = 16,
  JSON_SHOW_DEVICES = 32,
  JSON_NO_UNDEFINED = 64,
  JSON_ARRAYBUFFER_AS_ARRAY = 128,
  JSON_SHOW_OBJECT_NAMES = 256,
  JSON_DROP_QUOTES = 512,
  JSON_PIN_TO_STRING = 1024,
  JSON_ALL_UNICODE_ESCAPE = 2048,
  JSON_NO_NAN = 4096,
  JSON_JSON_COMPATIBILE = JSON_PIN_TO_STRING|JSON_ALL_UNICODE_ESCAPE|JSON_NO_NAN,
  JSON_ALLOW_TOJSON = 8192,
  JSON_INDENT = 16384,
} JSONFlags;
JsVar *jswrap_json_stringify(JsVar *v, JsVar *replacer, JsVar *space);
JsVar *jswrap_json_parse_ext(JsVar *v, JSONFlags flags);
JsVar *jswrap_json_parse_liberal(JsVar *v, bool noExceptions);
JsVar *jswrap_json_parse(JsVar *v);
void jsfGetJSONForFunctionWithCallback(JsVar *var, JSONFlags flags, vcbprintf_callback user_callback, void *user_data);
void jsfGetJSONWithCallback(JsVar *var, JsVar *varName, JSONFlags flags, const char *whitespace, vcbprintf_callback user_callback, void *user_data);
void jsfGetJSONWhitespace(JsVar *var, JsVar *result, JSONFlags flags, const char *whitespace);
void jsfGetJSON(JsVar *var, JsVar *result, JSONFlags flags);
void jsfPrintJSON(JsVar *var, JSONFlags flags);
void jsfPrintJSONForFunction(JsVar *var, JSONFlags flags);
void jshInitDevices();
void jshResetDevices();
typedef enum {
  EV_NONE,
  EV_EXTI0,
  EV_EXTI_MAX = EV_EXTI0 + 16 - 1,
  EV_SERIAL_START,
  EV_LOOPBACKA = EV_SERIAL_START,
  EV_LOOPBACKB,
  EV_LIMBO,
  EV_SERIAL_DEVICE_STATE_START,
  _EV_SERIAL_DEVICE_STATE_START_MINUS_ONE=EV_SERIAL_DEVICE_STATE_START-1,
  EV_SERIAL1,
  EV_SERIAL2,
  EV_SERIAL3,
  EV_SERIAL4,
  EV_SERIAL5,
  EV_SERIAL6,
  EV_SERIAL_MAX = EV_SERIAL1 + 6 - 1,
  EV_SERIAL1_STATUS,
  EV_SERIAL_STATUS_MAX = EV_SERIAL1_STATUS + 6 - 1,
  EV_CUSTOM,
  EV_SPI1,
  EV_SPI2,
  EV_SPI3,
  EV_SPI_MAX = EV_SPI1 + 3 - 1,
  EV_I2C1,
  EV_I2C2,
  EV_I2C3,
  EV_I2C_MAX = EV_I2C1 + 3 - 1,
  EV_DEVICE_MAX,
  EV_TYPE_MASK = (((EV_DEVICE_MAX) | (EV_DEVICE_MAX)>>1 | (EV_DEVICE_MAX)>>2 | (EV_DEVICE_MAX)>>3 | (EV_DEVICE_MAX)>>4 | (EV_DEVICE_MAX)>>5 | (EV_DEVICE_MAX)>>6 | (EV_DEVICE_MAX)>>7 | (EV_DEVICE_MAX)>>8 | (EV_DEVICE_MAX)>>9 | (EV_DEVICE_MAX)>>10 | (EV_DEVICE_MAX)>>11 | (EV_DEVICE_MAX)>>12 | (EV_DEVICE_MAX)>>13 | (EV_DEVICE_MAX)>>14 | (EV_DEVICE_MAX)>>15)+1) - 1,
  EV_SERIAL_STATUS_FRAMING_ERR = EV_TYPE_MASK+1,
  EV_SERIAL_STATUS_PARITY_ERR = EV_SERIAL_STATUS_FRAMING_ERR<<1,
  EV_EXTI_IS_HIGH = EV_TYPE_MASK+1,
  EV_EXTI_DATA_PIN_HIGH = EV_EXTI_IS_HIGH<<1
} __attribute__ ((__packed__)) IOEventFlags;
typedef enum {
  EVC_NONE,
  EVC_TYPE_MASK = 255,
  EVC_DATA_LPCOMP_UP = 256
} __attribute__ ((__packed__)) IOCustomEventFlags;
typedef unsigned char Pin;

typedef enum {
  JSH_PORT_NONE,
  JSH_PORTA=1,
  JSH_PORTB,
  JSH_PORTC,
  JSH_PORTD,
  JSH_PORTE,
  JSH_PORTF,
  JSH_PORTG,
  JSH_PORTH,
  JSH_PORTI,
  JSH_PORTV,
  JSH_PORT_MASK = 15,
  JSH_PIN_NEGATED = 16
} __attribute__ ((__packed__)) JsvPinInfoPort;
typedef enum {
  JSH_PIN0 = 0,
  JSH_PIN1,
  JSH_PIN2,
  JSH_PIN3,
  JSH_PIN4,
  JSH_PIN5,
  JSH_PIN6,
  JSH_PIN7,
  JSH_PIN8,
  JSH_PIN9,
  JSH_PIN10,
  JSH_PIN11,
  JSH_PIN12,
  JSH_PIN13,
  JSH_PIN14,
  JSH_PIN15,
} __attribute__ ((__packed__)) JsvPinInfoPin;
typedef enum {
  JSH_ANALOG_NONE = 0,
  JSH_ANALOG1 = 32,
  JSH_ANALOG_CH0 = 0,
  JSH_ANALOG_CH1,
  JSH_ANALOG_CH2,
  JSH_ANALOG_CH3,
  JSH_ANALOG_CH4,
  JSH_ANALOG_CH5,
  JSH_ANALOG_CH6,
  JSH_ANALOG_CH7,
  JSH_ANALOG_CH8,
  JSH_ANALOG_CH9,
  JSH_ANALOG_CH10,
  JSH_ANALOG_CH11,
  JSH_ANALOG_CH12,
  JSH_ANALOG_CH13,
  JSH_ANALOG_CH14,
  JSH_ANALOG_CH15,
  JSH_ANALOG_CH16,
  JSH_ANALOG_CH17,
  JSH_MASK_ANALOG_CH = 31,
  JSH_MASK_ANALOG_ADC = JSH_ANALOG1,
} __attribute__ ((__packed__)) JsvPinInfoAnalog;
typedef enum {
  JSH_AF0 = 0,
  JSH_AF1,
  JSH_AF2,
  JSH_AF3,
  JSH_AF4,
  JSH_AF5,
  JSH_AF6,
  JSH_AF7,
  JSH_AF8,
  JSH_AF9,
  JSH_AF10,
  JSH_AF11,
  JSH_AF12,
  JSH_AF13,
  JSH_AF14,
  JSH_AF15,
  JSH_NOTHING = 0,
  JSH_TIMER1 = 0x0010,
  JSH_TIMER2 = 0x0020,
  JSH_TIMER3 = 0x0030,
  JSH_TIMER4 = 0x0040,
  JSH_TIMER5 = 0x0050,
  JSH_TIMER6 = 0x0060,
  JSH_TIMER7 = 0x0070,
  JSH_TIMER8 = 0x0080,
  JSH_TIMER9 = 0x0090,
  JSH_TIMER10 = 0x00A0,
  JSH_TIMER11 = 0x00B0,
  JSH_TIMER12 = 0x00C0,
  JSH_TIMER13 = 0x00D0,
  JSH_TIMER14 = 0x00E0,
  JSH_TIMER15 = 0x00F0,
  JSH_TIMER16 = 0x0100,
  JSH_TIMER17 = 0x0110,
  JSH_TIMER18 = 0x0120,
  JSH_TIMERMAX = JSH_TIMER18,
  JSH_DAC = 0x0180,
  JSH_SPI1 = 0x0200,
  JSH_SPI2 = 0x0210,
  JSH_SPI3 = 0x0220,
  JSH_SPIMAX = JSH_SPI3,
  JSH_I2C1 = 0x0280,
  JSH_I2C2 = 0x0290,
  JSH_I2C3 = 0x02A0,
  JSH_I2C4 = 0x02B0,
  JSH_I2CMAX = JSH_I2C4,
  JSH_USART1 = 0x0300,
  JSH_USART2 = 0x0310,
  JSH_USART3 = 0x0320,
  JSH_USART4 = 0x0330,
  JSH_USART5 = 0x0340,
  JSH_USART6 = 0x0350,
  JSH_USARTMAX = JSH_USART6,
  JSH_TIMER_CH1 = 0x0000,
  JSH_TIMER_CH2 = 0x1000,
  JSH_TIMER_CH3 = 0x2000,
  JSH_TIMER_CH4 = 0x3000,
  JSH_MASK_TIMER_CH = 0x7000,
  JSH_TIMER_NEGATED = 0x8000,
  JSH_USART_RX = 0x0000,
  JSH_USART_TX = 0x1000,
  JSH_USART_CK = 0x2000,
  JSH_SPI_MISO = 0x0000,
  JSH_SPI_MOSI = 0x1000,
  JSH_SPI_SCK = 0x2000,
  JSH_I2C_SCL = 0x0000,
  JSH_I2C_SDA = 0x1000,
  JSH_DAC_CH1 = 0x0000,
  JSH_DAC_CH2 = 0x1000,
  JSH_MASK_AF = 0x000F,
  JSH_MASK_TYPE = 0x0FF0,
  JSH_MASK_INFO = 0xF000,
  JSH_SHIFT_TYPE = 4,
  JSH_SHIFT_INFO = 12,
} __attribute__ ((__packed__)) JshPinFunction;
typedef enum {
  JSHPINSTATE_UNDEFINED,
  JSHPINSTATE_GPIO_OUT,
  JSHPINSTATE_GPIO_OUT_OPENDRAIN,
  JSHPINSTATE_GPIO_OUT_OPENDRAIN_PULLUP,
  JSHPINSTATE_GPIO_IN,
  JSHPINSTATE_GPIO_IN_PULLUP,
  JSHPINSTATE_GPIO_IN_PULLDOWN,
  JSHPINSTATE_ADC_IN,
  JSHPINSTATE_AF_OUT,
  JSHPINSTATE_AF_OUT_OPENDRAIN,
  JSHPINSTATE_USART_IN,
  JSHPINSTATE_USART_OUT,
  JSHPINSTATE_DAC_OUT,
  JSHPINSTATE_I2C,
  JSHPINSTATE_MASK = (((JSHPINSTATE_I2C) | (JSHPINSTATE_I2C)>>1 | (JSHPINSTATE_I2C)>>2 | (JSHPINSTATE_I2C)>>3 | (JSHPINSTATE_I2C)>>4 | (JSHPINSTATE_I2C)>>5 | (JSHPINSTATE_I2C)>>6 | (JSHPINSTATE_I2C)>>7 | (JSHPINSTATE_I2C)>>8 | (JSHPINSTATE_I2C)>>9 | (JSHPINSTATE_I2C)>>10 | (JSHPINSTATE_I2C)>>11 | (JSHPINSTATE_I2C)>>12 | (JSHPINSTATE_I2C)>>13 | (JSHPINSTATE_I2C)>>14 | (JSHPINSTATE_I2C)>>15)+1)-1,
  JSHPINSTATE_PIN_IS_ON = JSHPINSTATE_MASK+1,
} __attribute__ ((__packed__)) JshPinState;
bool jshIsPinValid(Pin pin);
Pin jshGetPinFromString(const char *s);
void jshGetPinString(char *result, Pin pin);
Pin jshGetPinFromVar(JsVar *pinv);
Pin jshGetPinFromVarAndUnLock(JsVar *pinv);
bool jshGetPinStateIsManual(Pin pin);
void jshSetPinStateIsManual(Pin pin, bool manual);
bool jshGetPinShouldStayWatched(Pin pin);
void jshSetPinShouldStayWatched(Pin pin, bool manual);
void jshResetPinStateIsManual();
bool jshPinInput(Pin pin);
void jshPinOutput(Pin pin, bool value);
JshPinFunction jshGetPinFunctionFromDevice(IOEventFlags device);
IOEventFlags jshGetFromDevicePinFunction(JshPinFunction func);
JshPinFunction __attribute__ ((noinline)) jshGetPinFunctionForPin(Pin pin, JshPinFunction functionType);
Pin __attribute__ ((noinline)) jshFindPinForFunction(JshPinFunction functionType, JshPinFunction functionInfo);
typedef enum {
  JSPFTS_DEVICE = 1,
  JSPFTS_DEVICE_NUMBER = 2,
  JSPFTS_SPACE = 4,
  JSPFTS_TYPE = 8,
  JSPFTS_JS_NAMES = 16
} JshPinFunctionToStringFlags;
void jshPinFunctionToString(JshPinFunction pinFunc, JshPinFunctionToStringFlags flags, char *buf, size_t bufSize);
void __attribute__ ((noinline)) jshPrintCapablePins(Pin existingPin, const char *functionName, JshPinFunction typeMin, JshPinFunction typeMax, JshPinFunction pMask, JshPinFunction pData, bool printAnalogs);
JshPinFunction jshGetDeviceFor(JshPinFunction deviceMin, JshPinFunction deviceMax, Pin pin);
JsVar *jshGetDeviceObjectFor(JshPinFunction deviceMin, JshPinFunction deviceMax, Pin pin);
JsVar *jshGetPinStateString(JshPinState state);
bool jshPushEvent(IOEventFlags evt, uint8_t *data, unsigned int length);
void jshPushIOEvent(IOEventFlags channel, JsSysTime time);
void jshPushIOWatchEvent(IOEventFlags channel);
void jshPushIOCharEvent(IOEventFlags channel, char ch);
void jshPushIOCharEvents(IOEventFlags channel, char *data, unsigned int count);
IOEventFlags jshPopIOEvent(uint8_t *data, unsigned int *length);
IOEventFlags jshPopIOEventOfType(IOEventFlags eventType, uint8_t *data, unsigned int *length);
bool jshHasEvents();
bool jshIsTopEvent(IOEventFlags eventType);
int jshGetEventsUsed();
bool jshHasEventSpaceForChars(int n);
int jshGetIOCharEventsFree();
const char *jshGetDeviceString(IOEventFlags device);
IOEventFlags jshFromDeviceString(const char *device);
JsVar *jshGetDeviceObject(IOEventFlags device);
void jshTransmit(IOEventFlags device, unsigned char data);
void jshTransmitPrintf(IOEventFlags device, const char *fmt, ...);
void jshTransmitFlush();
void jshTransmitFlushDevice(IOEventFlags device);
void jshTransmitClearDevice(IOEventFlags device);
void jshTransmitMove(IOEventFlags from, IOEventFlags to);
bool jshHasTransmitData();
IOEventFlags jshGetDeviceToTransmit();
int jshGetCharToTransmit(IOEventFlags device);
void jshSetFlowControlXON(IOEventFlags device, bool hostShouldTransmit);
void jshSetFlowControlAllReady();
void jshSetFlowControlEnabled(IOEventFlags device, bool software, unsigned char pinCTS);
typedef void(*JshEventCallbackCallback)(bool state, IOEventFlags flags);
void jshSetEventCallback(IOEventFlags channel, JshEventCallbackCallback callback);
void jshSetEventDataPin(IOEventFlags channel, Pin pin);
Pin jshGetEventDataPin(IOEventFlags channel);
void jshSetErrorHandlingEnabled(IOEventFlags device, bool errorHandling);
bool jshGetErrorHandlingEnabled(IOEventFlags device);

typedef struct JshPinInfo {
  JsvPinInfoPort port;
  JsvPinInfoPin pin;
  JsvPinInfoAnalog analog;
  JshPinFunction functions[0];
} __attribute__ ((__packed__)) JshPinInfo;
extern const JshPinInfo pinInfo[33];
void jshInit();
void jshReset();
void jshIdle();
void jshBusyIdle();
bool jshSleep(JsSysTime timeUntilWake);
void jshKill();
int jshGetSerialNumber(unsigned char *data, int maxChars);
bool jshIsUSBSERIALConnected();
JsSysTime jshGetSystemTime();
void jshSetSystemTime(JsSysTime time);
JsSysTime jshGetTimeFromMilliseconds(JsVarFloat ms);
JsVarFloat jshGetMillisecondsFromTime(JsSysTime time);
void jshInterruptOff();
void jshInterruptOn();
bool jshIsInInterrupt();
void jshDelayMicroseconds(int microsec);
void jshPinSetValue(Pin pin, bool value);
bool jshPinGetValue(Pin pin);
void jshPinSetState(Pin pin, JshPinState state);
JshPinState jshPinGetState(Pin pin);
bool jshIsPinStateDefault(Pin pin, JshPinState state);
JsVarFloat jshPinAnalog(Pin pin);
int jshPinAnalogFast(Pin pin);
typedef enum {
  JSAOF_NONE,
  JSAOF_ALLOW_SOFTWARE = 1,
  JSAOF_FORCE_SOFTWARE = 2,
} JshAnalogOutputFlags;
JshPinFunction jshPinAnalogOutput(Pin pin, JsVarFloat value, JsVarFloat freq, JshAnalogOutputFlags flags);
typedef enum {
  JSPW_NONE,
  JSPW_HIGH_SPEED = 1,
} JshPinWatchFlags;
bool jshCanWatch(Pin pin);
IOEventFlags jshPinWatch(Pin pin, bool shouldWatch, JshPinWatchFlags flags);
JshPinFunction jshGetCurrentPinFunction(Pin pin);
void jshSetOutputValue(JshPinFunction func, int value);
void jshEnableWatchDog(JsVarFloat timeout);
void jshKickWatchDog();
void jshKickSoftWatchDog();
bool jshGetWatchedPinState(IOEventFlags device);
bool jshIsEventForPin(IOEventFlags eventFlags, Pin pin);
bool jshIsDeviceInitialised(IOEventFlags device);
typedef struct {
  int baudRate;
  Pin pinRX;
  Pin pinTX;
  Pin pinCK;
  Pin pinCTS;
  unsigned char bytesize;
  unsigned char parity;
  unsigned char stopbits;
  bool xOnXOff;
  bool errorHandling;
} __attribute__ ((__packed__)) JshUSARTInfo;
void jshUSARTInitInfo(JshUSARTInfo *inf);
void jshUSARTSetup(IOEventFlags device, JshUSARTInfo *inf);
void jshUSARTUnSetup(IOEventFlags device);
void jshUSARTKick(IOEventFlags device);
typedef enum {
  SPIF_CPHA = 1,
  SPIF_CPOL = 2,
  SPIF_SPI_MODE_0 = 0,
  SPIF_SPI_MODE_1 = SPIF_CPHA,
  SPIF_SPI_MODE_2 = SPIF_CPOL,
  SPIF_SPI_MODE_3 = SPIF_CPHA | SPIF_CPOL,
} __attribute__ ((__packed__)) JshSPIFlags;
typedef enum {
  SPIB_DEFAULT = 0,
  SPIB_MAXIMUM,
  SPIB_MINIMUM,
} __attribute__ ((__packed__)) JshBaudFlags;
typedef struct {
  int baudRate;
  JshBaudFlags baudRateSpec;
  Pin pinSCK;
  Pin pinMISO;
  Pin pinMOSI;
  unsigned char spiMode;
  bool spiMSB;
  int numBits;
} __attribute__ ((__packed__)) JshSPIInfo;
void jshSPIInitInfo(JshSPIInfo *inf);
void jshSPISetup(IOEventFlags device, JshSPIInfo *inf);
int jshSPISend(IOEventFlags device, int data);
void jshSPISend16(IOEventFlags device, int data);
bool jshSPISendMany(IOEventFlags device, unsigned char *tx, unsigned char *rx, size_t count, void (*callback)());
void jshSPISet16(IOEventFlags device, bool is16);
void jshSPISetReceive(IOEventFlags device, bool isReceive);
void jshSPIWait(IOEventFlags device);
typedef struct {
  int bitrate;
  Pin pinSCL;
  Pin pinSDA;
  bool started;
  bool clockStretch;
} __attribute__ ((__packed__)) JshI2CInfo;
void jshI2CInitInfo(JshI2CInfo *inf);
void jshI2CSetup(IOEventFlags device, JshI2CInfo *inf);
void jshI2CUnSetup(IOEventFlags device);
void jshI2CWrite(IOEventFlags device, unsigned char address, int nBytes, const unsigned char *data, bool sendStop);
void jshI2CRead(IOEventFlags device, unsigned char address, int nBytes, unsigned char *data, bool sendStop);
bool jshFlashGetPage(uint32_t addr, uint32_t *startAddr, uint32_t *pageSize);
JsVar *jshFlashGetFree();
void jshFlashErasePage(uint32_t addr);
bool jshFlashErasePages(uint32_t addr, uint32_t byteLength);
void jshFlashRead(void *buf, uint32_t addr, uint32_t len);
void jshFlashWrite(void *buf, uint32_t addr, uint32_t len);
void jshFlashWriteAligned(void *buf, uint32_t addr, uint32_t len);
size_t jshFlashGetMemMapAddress(size_t ptr);
void jshUtilTimerStart(JsSysTime period);
void jshUtilTimerReschedule(JsSysTime period);
void jshUtilTimerDisable();
extern void jshHadEvent();
extern volatile bool jshHadEventDuringSleep;
JsVarFloat jshReadTemperature();
JsVarFloat jshReadVRef();
JsVarFloat jshReadVDDH();
unsigned int jshGetRandomNumber();
unsigned int jshSetSystemClock(JsVar *options);
JsVar *jshGetSystemClock();
void jsvGetProcessorPowerUsage(JsVar *devices);
void jshReboot();
void jsiInit(bool autoLoad);
void jsiKill();
void jsiOneSecondAfterStartup();
bool jsiLoop();
bool jsiFreeMoreMemory();
bool jsiHasTimers();
bool jsiIsWatchingPin(Pin pin);
void jsiCtrlC();
void jsiQueueEvents(JsVar *object, JsVar *callback, JsVar **args, int argCount);
bool jsiObjectHasCallbacks(JsVar *object, const char *callbackName);
void jsiQueueObjectCallbacks(JsVar *object, const char *callbackName, JsVar **args, int argCount);
bool jsiExecuteEventCallback(JsVar *thisVar, JsVar *callbackVar, unsigned int argCount, JsVar **argPtr);
bool jsiExecuteEventCallbackArgsArray(JsVar *thisVar, JsVar *callbackVar, JsVar *argsArray);
bool jsiExecuteEventCallbackName(JsVar *obj, const char *cbName, unsigned int argCount, JsVar **argPtr);
bool jsiExecuteEventCallbackOn(const char *objectName, const char *cbName, unsigned int argCount, JsVar **argPtr);
void jsiCheckErrors(bool wasREPL);
JsVar *jsiSetTimeout(void (*functionPtr)(void), JsVarFloat milliseconds);
void jsiClearTimeout(JsVar *timeout);
IOEventFlags jsiGetDeviceFromClass(JsVar *deviceClass);
JsVar *jsiGetClassNameFromDevice(IOEventFlags device);
IOEventFlags jsiGetPreferredConsoleDevice();
void jsiSetConsoleDevice(IOEventFlags device, bool force);
IOEventFlags jsiGetConsoleDevice();
bool jsiIsConsoleDeviceForced();
void jsiConsolePrintChar(char data);
void jsiConsolePrintString(const char *str);
void vcbprintf_callback_jsiConsolePrintString(const char *str, void* user_data);
void jsiConsolePrintf(const char *fmt, ...);
void jsiConsolePrintStringVar(JsVar *v);
void jsiConsoleRemoveInputLine();
void jsiReplaceInputLine(JsVar *newLine);
void jsiClearInputLine(bool updateConsole);
typedef enum {
  BUSY_INTERACTIVE = 1,
  BUSY_TRANSMIT = 2,
} __attribute__ ((__packed__)) JsiBusyDevice;
void jsiSetBusy(JsiBusyDevice device, bool isBusy);
typedef enum {
  JSI_SLEEP_AWAKE = 0,
  JSI_SLEEP_ASLEEP = 1,
  JSI_SLEEP_DEEP = 2,
} __attribute__ ((__packed__)) JsiSleepType;
void jsiSetSleep(JsiSleepType isSleep);
typedef enum {
  JSIS_NONE,
  JSIS_ECHO_OFF = 1<<0,
  JSIS_ECHO_OFF_FOR_LINE = 1<<1,
  JSIS_TIMERS_CHANGED = 1<<2,
  JSIS_TODO_FLASH_SAVE = 1<<5,
  JSIS_TODO_FLASH_LOAD = 1<<6,
  JSIS_TODO_RESET = 1<<7,
  JSIS_TODO_MASK = JSIS_TODO_FLASH_SAVE|JSIS_TODO_FLASH_LOAD|JSIS_TODO_RESET,
  JSIS_CONSOLE_FORCED = 1<<8,
  JSIS_WATCHDOG_AUTO = 1<<9,
  JSIS_PASSWORD_PROTECTED = 1<<10,
  JSIS_COMPLETELY_RESET = 1<<11,
  JSIS_FIRST_BOOT = 1<<12,
  JSIS_EVENTEMITTER_PROCESSING = 1<<13,
  JSIS_EVENTEMITTER_STOP = 1<<14,
  JSIS_EVENTEMITTER_INTERRUPTED = 1<<15,
  JSIS_ECHO_OFF_MASK = JSIS_ECHO_OFF|JSIS_ECHO_OFF_FOR_LINE,
  JSIS_SOFTINIT_MASK = JSIS_PASSWORD_PROTECTED|JSIS_WATCHDOG_AUTO|JSIS_TODO_MASK|JSIS_FIRST_BOOT|JSIS_COMPLETELY_RESET
} __attribute__ ((__packed__)) JsiStatus;
extern JsiStatus jsiStatus;
bool jsiEcho();
extern Pin pinBusyIndicator;
extern Pin pinSleepIndicator;
extern JsSysTime jsiLastIdleTime;
void jsiDumpJSON(vcbprintf_callback user_callback, void *user_data, JsVar *data, JsVar *existing);
void jsiDumpState(vcbprintf_callback user_callback, void *user_data);
extern JsVarRef timerArray;
extern JsVarRef watchArray;
extern JsVarInt jsiTimerAdd(JsVar *timerPtr);
extern void jsiTimersChanged();
typedef enum {
  JSWAT_FINISH = 0,
  JSWAT_VOID = 0,
  JSWAT_JSVAR,
  JSWAT_ARGUMENT_ARRAY,
  JSWAT_BOOL,
  JSWAT_INT32,
  JSWAT_PIN,
  JSWAT_FLOAT32,
  JSWAT_JSVARFLOAT,
  JSWAT__LAST = JSWAT_JSVARFLOAT,
  JSWAT_MASK = (((JSWAT__LAST) | (JSWAT__LAST)>>1 | (JSWAT__LAST)>>2 | (JSWAT__LAST)>>3 | (JSWAT__LAST)>>4 | (JSWAT__LAST)>>5 | (JSWAT__LAST)>>6 | (JSWAT__LAST)>>7 | (JSWAT__LAST)>>8 | (JSWAT__LAST)>>9 | (JSWAT__LAST)>>10 | (JSWAT__LAST)>>11 | (JSWAT__LAST)>>12 | (JSWAT__LAST)>>13 | (JSWAT__LAST)>>14 | (JSWAT__LAST)>>15)+1)-1,
  JSWAT_EXECUTE_IMMEDIATELY = 0x7000,
  JSWAT_EXECUTE_IMMEDIATELY_MASK = 0x7E00,
  JSWAT_THIS_ARG = 0x8000,
  JSWAT_ARGUMENTS_MASK = 0xFFFF ^ (JSWAT_MASK | JSWAT_THIS_ARG)
} __attribute__ ((__packed__)) JsnArgumentType;
typedef struct {
  unsigned short strOffset;
  unsigned short functionSpec;
  void (*functionPtr)(void);
} __attribute__((aligned(2))) JswSymPtr;
typedef struct {
  const JswSymPtr *symbols;
  const char *symbolChars;
  unsigned char symbolCount;
} __attribute__((aligned(2))) JswSymList;
JsVar *jswBinarySearch(const JswSymList *symbolsPtr, JsVar *parent, const char *name);
JsVar *jswFindBuiltInFunction(JsVar *parent, const char *name);
const JswSymList *jswGetSymbolListForObject(JsVar *parent);
const JswSymList *jswGetSymbolListForObjectProto(JsVar *parent);
bool jswIsBuiltInObject(const char *name);
const char *jswGetBasicObjectName(JsVar *var);
const char *jswGetBasicObjectPrototypeName(const char *name);
bool jswIdle();
void jswHWInit();
void jswInit();
void jswKill();
void jswGetPowerUsage(JsVar *devices);
bool jswOnCharEvent(IOEventFlags channel, char charData);
void jswOnCustomEvent(IOEventFlags eventFlags, uint8_t *data, int dataLen);
void *jswGetBuiltInLibrary(const char *name);
const char *jswGetBuiltInJSLibrary(const char *name);
const char *jswGetBuiltInLibraryNames();
JsVar *jswCallFunctionHack(void *function, JsnArgumentType argumentSpecifier, JsVar *thisParam, JsVar **paramData, int paramCount);
JsVarInt jswrap_integer_valueOf(JsVar *v);
JsVarFloat jswrap_math_asin(JsVarFloat x);
double jswrap_math_mod(double x, double y);
double jswrap_math_pow(double x, double y);
JsVar *jswrap_math_round(double x);
double jswrap_math_sqrt(double x);
double jswrap_math_sin(double x);
double jswrap_math_cos(double x);
double jswrap_math_atan(double x);
double jswrap_math_atan2(double y, double x);
JsVarFloat jswrap_math_clip(JsVarFloat x, JsVarFloat min, JsVarFloat max);
JsVarFloat jswrap_math_minmax(JsVar *args, bool isMax);
int jswrap_math_sign(double x);
JsVar *jswrap_object_constructor(JsVar *value);
JsVar *jswrap_object_length(JsVar *parent);
JsVar *jswrap_object_valueOf(JsVar *parent);
JsVar *jswrap_object_toString(JsVar *parent, JsVar *arg0);
JsVar *jswrap_object_clone(JsVar *parent);
typedef enum {
  JSWOKPF_NONE,
  JSWOKPF_INCLUDE_NON_ENUMERABLE = 1,
  JSWOKPF_INCLUDE_PROTOTYPE = 2,
  JSWOKPF_NO_INCLUDE_ARRAYBUFFER = 4
} JswObjectKeysOrPropertiesFlags;
void jswrap_object_keys_or_property_names_cb(
    JsVar *obj,
    JswObjectKeysOrPropertiesFlags flags,
    void (*callback)(void *data, JsVar *name),
    void *data
);
JsVar *jswrap_object_keys_or_property_names(
    JsVar *obj,
    JswObjectKeysOrPropertiesFlags flags);
JsVar *jswrap_object_values_or_entries(JsVar *object, bool returnEntries);
JsVar *jswrap_object_fromEntries(JsVar *entries);
JsVar *jswrap_object_create(JsVar *proto, JsVar *propertiesObject);
JsVar *jswrap_object_getOwnPropertyDescriptor(JsVar *parent, JsVar *name);
JsVar *jswrap_object_getOwnPropertyDescriptors(JsVar *parent);
bool jswrap_object_hasOwnProperty(JsVar *parent, JsVar *name);
JsVar *jswrap_object_defineProperty(JsVar *parent, JsVar *propName, JsVar *desc);
JsVar *jswrap_object_defineProperties(JsVar *parent, JsVar *props);
JsVar *jswrap_object_getPrototypeOf(JsVar *object);
JsVar *jswrap_object_setPrototypeOf(JsVar *object, JsVar *proto);
JsVar *jswrap_object_assign(JsVar *args);
void jswrap_object_removeListener(JsVar *parent, JsVar *event, JsVar *callback);
void jswrap_object_removeAllListeners(JsVar *parent, JsVar *event);
void jswrap_object_removeAllListeners_cstr(JsVar *parent, const char *event);
void jswrap_function_replaceWith(JsVar *parent, JsVar *newFunc);
JsVar *jswrap_function_apply_or_call(JsVar *parent, JsVar *thisArg, JsVar *argsArray);
JsVar *jswrap_function_bind(JsVar *parent, JsVar *thisArg, JsVar *argsArray);
bool jswrap_boolean_constructor(JsVar *value);
void jswrap_object_addEventListener(JsVar *parent, const char *eventName, void (*callback)(), JsnArgumentType argTypes);
JsVar *jswrap_arraybuffer_constructor(JsVarInt byteLength);
JsVar *jswrap_typedarray_constructor(JsVarDataArrayBufferViewType type, JsVar *arr, JsVarInt byteOffset, JsVarInt length);
void jswrap_arraybufferview_set(JsVar *parent, JsVar *arr, int offset);
JsVar *jswrap_arraybufferview_map(JsVar *parent, JsVar *funcVar, JsVar *thisVar);
JsVar *jswrap_arraybufferview_subarray(JsVar *parent, JsVarInt begin, JsVar *endVar);
JsVar *jswrap_arraybufferview_sort(JsVar *array, JsVar *compareFn);
JsVar *jswrap_dataview_constructor(JsVar *buffer, int byteOffset, int byteLength);
JsVar *jswrap_dataview_get(JsVar *dataview, JsVarDataArrayBufferViewType type, int byteOffset, bool littleEndian);
void jswrap_dataview_set(JsVar *dataview, JsVarDataArrayBufferViewType type, int byteOffset, JsVar *value, bool littleEndian);
unsigned int jsVarsSize = 0;
JsVar *jsVars = NULL;
typedef enum {
  MEM_NOT_BUSY,
  MEMBUSY_SYSTEM,
  MEMBUSY_GC
} __attribute__ ((__packed__)) MemBusyType;
volatile bool touchedFreeList = false;
volatile JsVarRef jsVarFirstEmpty;
volatile MemBusyType isMemoryBusy;
JsVarRef jsvGetFirstChild(const JsVar *v) { return v->varData.ref.firstChild; }
JsVarRefSigned jsvGetFirstChildSigned(const JsVar *v) {
  if (v->varData.ref.firstChild > ((1<<(14 -1))-1))
    return ((JsVarRefSigned)v->varData.ref.firstChild) + (-(1<<(14 -1)))*2;
  return (JsVarRefSigned)v->varData.ref.firstChild;
}
JsVarRef jsvGetLastChild(const JsVar *v) { return v->varData.ref.lastChild; }
JsVarRef jsvGetNextSibling(const JsVar *v) { return v->varData.ref.nextSibling; }
JsVarRef jsvGetPrevSibling(const JsVar *v) { return v->varData.ref.prevSibling; }
void jsvSetFirstChild(JsVar *v, JsVarRef r) { v->varData.ref.firstChild = r; }
void jsvSetLastChild(JsVar *v, JsVarRef r) { v->varData.ref.lastChild = r; }
void jsvSetNextSibling(JsVar *v, JsVarRef r) { v->varData.ref.nextSibling = r; }
void jsvSetPrevSibling(JsVar *v, JsVarRef r) { v->varData.ref.prevSibling = r; }
JsVarRefCounter jsvGetRefs(JsVar *v) { return v->varData.ref.refs; }
void jsvSetRefs(JsVar *v, JsVarRefCounter refs) { v->varData.ref.refs = refs; }
unsigned char jsvGetLocks(JsVar *v) { return (unsigned char)((v->flags>>JSV_LOCK_SHIFT) & 15); }
bool jsvIsRoot(const JsVar *v) { if (!v) return false; char f = v->flags&JSV_VARTYPEMASK; return (f)==JSV_ROOT; }
bool jsvIsPin(const JsVar *v) { if (!v) return false; char f = v->flags&JSV_VARTYPEMASK; ( (void)(f) ); return false; }
bool jsvIsSimpleInt(const JsVar *v) { return v && (v->flags&JSV_VARTYPEMASK)==JSV_INTEGER; }
bool jsvIsInt(const JsVar *v) { if (!v) return false; char f = v->flags&JSV_VARTYPEMASK; return ((f)==JSV_INTEGER || false || (f)==JSV_NAME_INT || (f)==JSV_NAME_INT_INT || (f)==JSV_NAME_INT_BOOL); }
bool jsvIsFloat(const JsVar *v) { return v && (v->flags&JSV_VARTYPEMASK)==JSV_FLOAT; }
bool jsvIsBoolean(const JsVar *v) { if (!v) return false; char f = v->flags&JSV_VARTYPEMASK; return ((f)==JSV_BOOLEAN || (f)==JSV_NAME_INT_BOOL); }
bool jsvIsString(const JsVar *v) { if (!v) return false; char f = v->flags&JSV_VARTYPEMASK; return ((f)>=_JSV_STRING_START && (f)<=_JSV_STRING_END); }
bool jsvIsUTF8String(const JsVar *v) { if (!v) return false; char f = v->flags&JSV_VARTYPEMASK; return false; }
bool jsvIsBasicString(const JsVar *v) { if (!v) return false; char f = v->flags&JSV_VARTYPEMASK; return ((f)>=JSV_STRING_0 && (f)<=JSV_STRING_MAX); }
bool jsvIsStringExt(const JsVar *v) { if (!v) return false; char f = v->flags&JSV_VARTYPEMASK; return ((f)>=JSV_STRING_EXT_0 && (f)<=JSV_STRING_EXT_MAX); }
bool jsvIsFlatString(const JsVar *v) { if (!v) return false; char f = v->flags&JSV_VARTYPEMASK; return (f)==JSV_FLAT_STRING; }
bool jsvIsNativeString(const JsVar *v) { if (!v) return false; char f = v->flags&JSV_VARTYPEMASK; return (f)==JSV_NATIVE_STRING; }
bool jsvIsFlashString(const JsVar *v) { if (!v) return false; char f = v->flags&JSV_VARTYPEMASK; ( (void)(f) ); return false; }
bool jsvIsConstant(const JsVar *v) { return v && (v->flags&JSV_CONSTANT)==JSV_CONSTANT; }
bool jsvIsNumeric(const JsVar *v) { if (!v) return false; char f = v->flags&JSV_VARTYPEMASK; return f>=_JSV_NUMERIC_START && f<=_JSV_NUMERIC_END; }
bool jsvIsFunction(const JsVar *v) { if (!v) return false; char f = v->flags&JSV_VARTYPEMASK; return ((f)==JSV_FUNCTION || (f)==JSV_FUNCTION_RETURN || (f)==JSV_NATIVE_FUNCTION); }
bool jsvIsFunctionReturn(const JsVar *v) { return v && ((v->flags&JSV_VARTYPEMASK)==JSV_FUNCTION_RETURN); }
bool jsvIsFunctionParameter(const JsVar *v) { return v && (v->flags&JSV_NATIVE) && jsvIsString(v); }
bool jsvIsObject(const JsVar *v) { if (!v) return false; char f = v->flags&JSV_VARTYPEMASK; return ((f)==JSV_OBJECT || (f)==JSV_ROOT); }
bool jsvIsArray(const JsVar *v) { return v && (v->flags&JSV_VARTYPEMASK)==JSV_ARRAY; }
bool jsvIsArrayBuffer(const JsVar *v) { return v && (v->flags&JSV_VARTYPEMASK)==JSV_ARRAYBUFFER; }
bool jsvIsArrayBufferName(const JsVar *v) { return v && (v->flags&(JSV_VARTYPEMASK))==JSV_ARRAYBUFFERNAME; }
bool jsvIsNativeFunction(const JsVar *v) { return v && (v->flags&(JSV_VARTYPEMASK))==JSV_NATIVE_FUNCTION; }
bool jsvIsUndefined(const JsVar *v) { return v==0; }
bool jsvIsNull(const JsVar *v) { return v && (v->flags&JSV_VARTYPEMASK)==JSV_NULL; }
bool jsvIsNullish(const JsVar *v) { return !v || (v->flags&JSV_VARTYPEMASK)==JSV_NULL; }
bool jsvIsBasic(const JsVar *v) { if (!v) return false; char f = v->flags&JSV_VARTYPEMASK; return ((f)>=_JSV_NUMERIC_START && (f)<=_JSV_NUMERIC_END) || ((f)>=_JSV_STRING_START && (f)<=_JSV_STRING_END); }
bool jsvIsName(const JsVar *v) { if (!v) return false; char f = v->flags&JSV_VARTYPEMASK; return ((f)>=_JSV_NAME_START && (f)<=_JSV_NAME_END); }
bool jsvIsBasicName(const JsVar *v) { if (!v) return false; char f = v->flags&JSV_VARTYPEMASK; return f>=JSV_NAME_STRING_0 && f<=JSV_NAME_STRING_MAX; }
bool jsvIsNameWithValue(const JsVar *v) { if (!v) return false; char f = v->flags&JSV_VARTYPEMASK; return ((f)>=_JSV_NAME_WITH_VALUE_START && (f)<=_JSV_NAME_WITH_VALUE_END); }
bool jsvIsNameInt(const JsVar *v) { if (!v) return false; char f = v->flags&JSV_VARTYPEMASK; return (f==JSV_NAME_INT_INT || (f>=JSV_NAME_STRING_INT_0 && f<=JSV_NAME_STRING_INT_MAX)); }
bool jsvIsNameIntInt(const JsVar *v) { return v && (v->flags&JSV_VARTYPEMASK)==JSV_NAME_INT_INT; }
bool jsvIsNameIntBool(const JsVar *v) { return v && (v->flags&JSV_VARTYPEMASK)==JSV_NAME_INT_BOOL; }
bool jsvIsNewChild(const JsVar *v) { return jsvIsName(v) && jsvGetNextSibling(v) && jsvGetNextSibling(v)==jsvGetPrevSibling(v); }
bool jsvIsGetterOrSetter(const JsVar *v) { if (!v) return false; char f = v->flags&JSV_VARTYPEMASK; ( (void)(f) ); return (f)==JSV_GET_SET; }
bool jsvIsRefUsedForData(const JsVar *v) {
  return jsvIsStringExt(v) || (jsvIsString(v)&&!jsvIsName(v)) || jsvIsFloat(v) ||
         jsvIsNativeFunction(v) || jsvIsArrayBuffer(v) || jsvIsArrayBufferName(v);
}
bool jsvIsIntegerish(const JsVar *v) {
  if (!v) return false;
  char f = v->flags&JSV_VARTYPEMASK;
  return ((f)==JSV_INTEGER || false || (f)==JSV_NAME_INT || (f)==JSV_NAME_INT_INT || (f)==JSV_NAME_INT_BOOL) || false || ((f)==JSV_BOOLEAN || (f)==JSV_NAME_INT_BOOL) || (f)==JSV_NULL;
}
bool jsvIsIterable(const JsVar *v) {
  if (!v) return false;
  char f = v->flags&JSV_VARTYPEMASK;
  return
      (f)==JSV_ARRAY || ((f)==JSV_OBJECT || (f)==JSV_ROOT) || ((f)==JSV_FUNCTION || (f)==JSV_FUNCTION_RETURN || (f)==JSV_NATIVE_FUNCTION) ||
      ((f)>=_JSV_STRING_START && (f)<=_JSV_STRING_END) || (f)==JSV_ARRAYBUFFER;
}
bool jsvHasCharacterData(const JsVar *v) {
  if (!v) return false;
  char f = v->flags&JSV_VARTYPEMASK;
  return ((f)>=_JSV_STRING_START && (f)<=_JSV_STRING_END) || ((f)>=JSV_STRING_EXT_0 && (f)<=JSV_STRING_EXT_MAX);
}
bool jsvHasStringExt(const JsVar *v) {
  if (!v) return false;
    char f = v->flags&JSV_VARTYPEMASK;
  return (((f)>=_JSV_STRING_START && (f)<=_JSV_STRING_END) || ((f)>=JSV_STRING_EXT_0 && (f)<=JSV_STRING_EXT_MAX) || false) && !((f)==JSV_FLAT_STRING || (f)==JSV_NATIVE_STRING || false);
}
bool jsvHasChildren(const JsVar *v) {
  if (!v) return false;
  char f = v->flags&JSV_VARTYPEMASK;
  return ((f)==JSV_FUNCTION || (f)==JSV_FUNCTION_RETURN || (f)==JSV_NATIVE_FUNCTION) || ((f)==JSV_OBJECT || (f)==JSV_ROOT) || (f)==JSV_ARRAY ||
         (f)==JSV_ROOT || (f)==JSV_GET_SET;
}
bool jsvHasSingleChild(const JsVar *v) {
  if (!v) return false;
  char f = v->flags&JSV_VARTYPEMASK;
  return (f)==JSV_ARRAYBUFFER ||
      (((f)>=_JSV_NAME_START && (f)<=_JSV_NAME_END) && !((f)>=_JSV_NAME_WITH_VALUE_START && (f)<=_JSV_NAME_WITH_VALUE_END));
}
static JsVar *jsvGetAddressOf(JsVarRef ref) {
  do { if (!(ref)) jsAssertFail("bin/espruino_embedded.c",1857,""); } while(0);
  do { if (!(ref <= jsVarsSize)) jsAssertFail("bin/espruino_embedded.c",1863,""); } while(0);
  return &jsVars[ref-1];
}
JsVar *_jsvGetAddressOf(JsVarRef ref) {
  return jsvGetAddressOf(ref);
}
void jsvSetMaxVarsUsed(unsigned int size) {
  do { if (!(size < 16000)) jsAssertFail("bin/espruino_embedded.c",1882,""); } while(0);
  jsVarsSize = size;
}
void jsvCreateEmptyVarList() {
  do { if (!(!isMemoryBusy)) jsAssertFail("bin/espruino_embedded.c",1893,""); } while(0);
  isMemoryBusy = MEMBUSY_SYSTEM;
  jsVarFirstEmpty = 0;
  JsVar firstVar;
  jsvSetNextSibling(&firstVar, 0);
  JsVar *lastEmpty = &firstVar;
  JsVarRef i;
  for (i=1;i<=jsVarsSize;i++) {
    JsVar *var = jsvGetAddressOf(i);
    if ((var->flags&JSV_VARTYPEMASK) == JSV_UNUSED) {
      jsvSetNextSibling(lastEmpty, i);
      lastEmpty = var;
    } else if (jsvIsFlatString(var)) {
      i = (JsVarRef)(i+jsvGetFlatStringBlocks(var));
    }
  }
  jsvSetNextSibling(lastEmpty, 0);
  jsVarFirstEmpty = jsvGetNextSibling(&firstVar);
  isMemoryBusy = MEM_NOT_BUSY;
}
void jsvClearEmptyVarList() {
  do { if (!(!isMemoryBusy)) jsAssertFail("bin/espruino_embedded.c",1920,""); } while(0);
  isMemoryBusy = MEMBUSY_SYSTEM;
  jsVarFirstEmpty = 0;
  JsVarRef i;
  for (i=1;i<=jsVarsSize;i++) {
    JsVar *var = jsvGetAddressOf(i);
    if ((var->flags&JSV_VARTYPEMASK) == JSV_UNUSED) {
      memset((void*)var,0,sizeof(JsVar));
    } else if (jsvIsFlatString(var)) {
      i = (JsVarRef)(i+jsvGetFlatStringBlocks(var));
    }
  }
  isMemoryBusy = MEM_NOT_BUSY;
}
void jsvSoftInit() {
  jsvCreateEmptyVarList();
}
void jsvSoftKill() {
  jsvClearEmptyVarList();
}
void jsvReset() {
  jsVarFirstEmpty = 0;
  memset(jsVars, 0, sizeof(JsVar)*jsVarsSize);
  jsvSoftInit();
}
void jsvInit(unsigned int size) {
  jsVarsSize = 16000;
  if (size) jsVarsSize = size;
  if(!jsVars) jsVars = (JsVar *)malloc(sizeof(JsVar) * jsVarsSize);
  jsvReset();
}
void jsvKill() {
  free(jsVars);
  jsVars = NULL;
  jsVarsSize = 0;
}
unsigned int jsvGetMemoryUsage() {
  unsigned int usage = 0;
  for (unsigned int i=1;i<=jsVarsSize;i++) {
    JsVar *v = jsvGetAddressOf((JsVarRef)i);
    if ((v->flags&JSV_VARTYPEMASK) != JSV_UNUSED) {
      usage++;
      if (jsvIsFlatString(v)) {
        unsigned int b = (unsigned int)jsvGetFlatStringBlocks(v);
        i+=b;
        usage+=b;
      }
    }
  }
  return usage;
}
unsigned int jsvGetMemoryTotal() {
  return jsVarsSize;
}
void jsvSetMemoryTotal(unsigned int jsNewVarCount) {
  ( (void)(jsNewVarCount) );
  do { if (!(0)) jsAssertFail("bin/espruino_embedded.c",2088,""); } while(0);
}
void jsvUpdateMemoryAddress(size_t oldAddr, size_t length, size_t newAddr) {
  for (unsigned int i=1;i<=jsVarsSize;i++) {
    JsVar *v = jsvGetAddressOf((JsVarRef)i);
    if (jsvIsNativeString(v) || jsvIsFlashString(v)) {
      size_t p = (size_t)v->varData.nativeStr.ptr;
      if (p>=oldAddr && p<oldAddr+length)
        v->varData.nativeStr.ptr = (char*)(p+newAddr-oldAddr);
    } else if (jsvIsFlatString(v)) {
      i += (unsigned int)jsvGetFlatStringBlocks(v);
    }
  }
}
bool jsvMoreFreeVariablesThan(unsigned int vars) {
  if (!vars) return false;
  JsVarRef r = jsVarFirstEmpty;
  while (r) {
    if (!vars--) return true;
    r = jsvGetNextSibling(jsvGetAddressOf(r));
  }
  return false;
}
bool jsvIsMemoryFull() {
  return !jsVarFirstEmpty;
}
void jsvShowAllocated() {
  JsVarRef i;
  for (i=1;i<=jsVarsSize;i++) {
    if ((jsvGetAddressOf(i)->flags&JSV_VARTYPEMASK) != JSV_UNUSED) {
      jsiConsolePrintf("USED VAR #%d:",i);
      jsvTrace(jsvGetAddressOf(i), 2);
    }
  }
}
size_t jsvGetMaxCharactersInVar(const JsVar *v) {
  if (jsvIsStringExt(v)) return (4 + ((14*3 + 0 + 8)>>3));
  do { if (!(jsvHasCharacterData(v))) jsAssertFail("bin/espruino_embedded.c",2137,""); } while(0);
  if (jsvIsName(v)) return 4;
  if (jsvIsFlatString(v) || jsvIsFlashString(v) || jsvIsNativeString(v)) return jsvGetCharactersInVar(v);
  return (4 + ((14*3 + 0)>>3));
}
size_t jsvGetCharactersInVar(const JsVar *v) {
  unsigned int f = v->flags&JSV_VARTYPEMASK;
  if (f == JSV_FLAT_STRING)
    return (size_t)v->varData.integer;
  if (false
  || (f == JSV_NATIVE_STRING)
      )
    return (size_t)v->varData.nativeStr.len;
  do { if (!(f >= JSV_NAME_STRING_INT_0)) jsAssertFail("bin/espruino_embedded.c",2156,""); } while(0);
  do { if (!((JSV_NAME_STRING_INT_0 < JSV_NAME_STRING_0) && (JSV_NAME_STRING_0 < JSV_STRING_0) && (JSV_STRING_0 < JSV_STRING_EXT_0))) jsAssertFail("bin/espruino_embedded.c",2159,""); } while(0);
  if (f<=JSV_NAME_STRING_MAX) {
    if (f<=JSV_NAME_STRING_INT_MAX)
      return f-JSV_NAME_STRING_INT_0;
    else
      return f-JSV_NAME_STRING_0;
  } else {
    if (f<=JSV_STRING_MAX) return f-JSV_STRING_0;
    do { if (!(f <= JSV_STRING_EXT_MAX)) jsAssertFail("bin/espruino_embedded.c",2167,""); } while(0);
    return f - JSV_STRING_EXT_0;
  }
}
void jsvSetCharactersInVar(JsVar *v, size_t chars) {
  unsigned int f = v->flags&JSV_VARTYPEMASK;
  do { if (!(!(jsvIsFlatString(v) || jsvIsNativeString(v) || jsvIsFlashString(v)))) jsAssertFail("bin/espruino_embedded.c",2175,""); } while(0);
  JsVarFlags m = (JsVarFlags)(v->flags&~JSV_VARTYPEMASK);
  do { if (!(f >= JSV_NAME_STRING_INT_0)) jsAssertFail("bin/espruino_embedded.c",2178,""); } while(0);
  do { if (!((JSV_NAME_STRING_INT_0 < JSV_NAME_STRING_0) && (JSV_NAME_STRING_0 < JSV_STRING_0) && (JSV_STRING_0 < JSV_STRING_EXT_0))) jsAssertFail("bin/espruino_embedded.c",2181,""); } while(0);
  if (f<=JSV_NAME_STRING_MAX) {
    do { if (!(chars <= 4)) jsAssertFail("bin/espruino_embedded.c",2183,""); } while(0);
    if (f<=JSV_NAME_STRING_INT_MAX)
      v->flags = (JsVarFlags)(m | (JSV_NAME_STRING_INT_0+chars));
    else
      v->flags = (JsVarFlags)(m | (JSV_NAME_STRING_0+chars));
  } else {
    if (f<=JSV_STRING_MAX) {
      do { if (!(chars <= (4 + ((14*3 + 0)>>3)))) jsAssertFail("bin/espruino_embedded.c",2190,""); } while(0);
      v->flags = (JsVarFlags)(m | (JSV_STRING_0+chars));
    } else {
      do { if (!(chars <= (4 + ((14*3 + 0 + 8)>>3)))) jsAssertFail("bin/espruino_embedded.c",2193,""); } while(0);
      do { if (!(f <= JSV_STRING_EXT_MAX)) jsAssertFail("bin/espruino_embedded.c",2194,""); } while(0);
      v->flags = (JsVarFlags)(m | (JSV_STRING_EXT_0+chars));
    }
  }
}
void jsvResetVariable(JsVar *v, JsVarFlags flags) {
  do { if (!((v->flags&JSV_VARTYPEMASK) == JSV_UNUSED)) jsAssertFail("bin/espruino_embedded.c",2201,""); } while(0);
  do { if (!(!(flags & JSV_LOCK_MASK))) jsAssertFail("bin/espruino_embedded.c",2202,""); } while(0);
  unsigned int i;
  if ((sizeof(JsVar)&3) == 0) {
    for (i=0;i<sizeof(JsVar)/sizeof(uint32_t);i++)
      ((uint32_t*)v)[i] = 0;
  } else {
    for (i=0;i<sizeof(JsVar);i++)
      ((uint8_t*)v)[i] = 0;
  }
  v->flags = flags | JSV_LOCK_ONE;
}
JsVar *jsvNewWithFlags(JsVarFlags flags) {
  if (isMemoryBusy) {
    jsErrorFlags |= JSERR_MEMORY_BUSY;
    return 0;
  }
  JsVar *v = 0;
  jshInterruptOff();
  if (jsVarFirstEmpty!=0) {
    v = jsvGetAddressOf(jsVarFirstEmpty);
    jsVarFirstEmpty = jsvGetNextSibling(v);
    touchedFreeList = true;
  }
  jshInterruptOn();
  if (v) {
    do { if (!(v->flags == JSV_UNUSED)) jsAssertFail("bin/espruino_embedded.c",2239,""); } while(0);
    jsvResetVariable(v, flags);
    return v;
  }
  jsErrorFlags |= JSERR_LOW_MEMORY;
  if (jshIsInInterrupt()) {
    return 0;
  }
  if (jsvGarbageCollect()) {
    return jsvNewWithFlags(flags);
  }
  if (jsiFreeMoreMemory()) {
    return jsvNewWithFlags(flags);
  }
  if (!(jsErrorFlags & JSERR_MEMORY)) {
    jsErrorFlags |= JSERR_MEMORY;
    jsiConsolePrintString("OUT OF MEMORY");
    jswrap_console_trace(NULL);
  }
  jsErrorFlags |= JSERR_MEMORY;
  jspSetInterrupted(true);
  return 0;
}
static void jsvFreePtrInternal(JsVar *var) {
  do { if (!(jsvGetLocks(var)==0)) jsAssertFail("bin/espruino_embedded.c",2290,""); } while(0);
  var->flags = JSV_UNUSED;
  jshInterruptOff();
  jsvSetNextSibling(var, jsVarFirstEmpty);
  jsVarFirstEmpty = jsvGetRef(var);
  touchedFreeList = true;
  jshInterruptOn();
}
void jsvFreePtrStringExt(JsVar* var) {
  JsVarRef ref = jsvGetLastChild(var);
  if (!ref) return;
  JsVar* ext = jsvGetAddressOf(ref);
  while (true) {
    ext->flags = JSV_UNUSED;
    ref = jsvGetLastChild(ext);
    if (!ref) break;
    jsvSetNextSibling(ext, ref);
    ext = jsvGetAddressOf(ref);
  }
  jshInterruptOff();
  jsvSetNextSibling(ext, jsVarFirstEmpty);
  jsVarFirstEmpty = jsvGetLastChild(var);
  touchedFreeList = true;
  jshInterruptOn();
}
              void jsvFreePtr(JsVar *var) {
  do { if (!((!jsvGetNextSibling(var) && !jsvGetPrevSibling(var)) || jsvIsRefUsedForData(var) || (jsvIsName(var) && (jsvGetNextSibling(var)==jsvGetPrevSibling(var))))) jsAssertFail("bin/espruino_embedded.c",2325,""); } while(0);
  if (jsvIsNameWithValue(var)) {
  } else if (jsvHasSingleChild(var)) {
    if (jsvGetFirstChild(var)) {
      if (jsuGetFreeStack() > 256) {
        JsVar *child = jsvLock(jsvGetFirstChild(var));
        jsvUnRef(child);
        jsvUnLock(child);
      }
    }
  }
  if (jsvIsUTF8String(var)) {
    jsvUnRefRef(jsvGetLastChild(var));
    jsvSetLastChild(var, 0);
  } else if (jsvHasStringExt(var)) {
    jsvFreePtrStringExt(var);
  } else if (jsvIsFlatString(var)) {
    size_t count = jsvGetFlatStringBlocks(var);
    JsVarRef i = (JsVarRef)(jsvGetRef(var)+count);
    jshInterruptOff();
    JsVarRef insertBefore = jsVarFirstEmpty;
    JsVarRef insertAfter = 0;
    while (insertBefore && insertBefore<i) {
      insertAfter = insertBefore;
      insertBefore = jsvGetNextSibling(jsvGetAddressOf(insertBefore));
    }
    while (count--) {
      JsVar *p = jsvGetAddressOf(i--);
      p->flags = JSV_UNUSED;
      jsvSetNextSibling(p, insertBefore);
      insertBefore = jsvGetRef(p);
    }
    if (insertAfter)
      jsvSetNextSibling(jsvGetAddressOf(insertAfter), insertBefore);
    else
      jsVarFirstEmpty = insertBefore;
    touchedFreeList = true;
    jshInterruptOn();
  }
  if (jsvHasChildren(var)) {
    JsVarRef childref = jsvGetLastChild(var);
    while (childref) {
      JsVar *child = jsvLock(childref);
      do { if (!(jsvIsName(child))) jsAssertFail("bin/espruino_embedded.c",2406,""); } while(0);
      childref = jsvGetPrevSibling(child);
      jsvSetPrevSibling(child, 0);
      jsvSetNextSibling(child, 0);
      jsvUnRef(child);
      jsvUnLock(child);
    }
  } else {
    if (jsvIsName(var)) {
      do { if (!(jsvGetNextSibling(var)==jsvGetPrevSibling(var))) jsAssertFail("bin/espruino_embedded.c",2419,""); } while(0);
      if (jsvGetNextSibling(var)) {
        jsvUnRefRef(jsvGetNextSibling(var));
        jsvUnRefRef(jsvGetPrevSibling(var));
      }
    }
  }
  jsvFreePtrInternal(var);
}
JsVarRef jsvGetRef(JsVar *var) {
  if (!var) return 0;
  return (JsVarRef)(1 + (var - jsVars));
}
JsVar *jsvLock(JsVarRef ref) {
  JsVar *var = jsvGetAddressOf(ref);
  if ((var->flags & JSV_LOCK_MASK)!=JSV_LOCK_MASK)
    var->flags += JSV_LOCK_ONE;
  return var;
}
JsVar *jsvLockSafe(JsVarRef ref) {
  if (!ref) return 0;
  return jsvLock(ref);
}
JsVar *jsvLockAgain(JsVar *var) {
  do { if (!(var)) jsAssertFail("bin/espruino_embedded.c",2471,""); } while(0);
  if ((var->flags & JSV_LOCK_MASK)!=JSV_LOCK_MASK)
    var->flags += JSV_LOCK_ONE;
  return var;
}
JsVar *jsvLockAgainSafe(JsVar *var) {
  return var ? jsvLockAgain(var) : 0;
}
static __attribute__ ((noinline)) void jsvUnLockFreeIfNeeded(JsVar *var) {
  do { if (!(jsvGetLocks(var) == 0)) jsAssertFail("bin/espruino_embedded.c",2485,""); } while(0);
  if (jsvGetRefs(var) == 0 &&
      jsvHasRef(var) &&
      (var->flags&JSV_VARTYPEMASK)!=JSV_UNUSED) {
    jsvFreePtr(var);
  }
}
static void jsvUnLockInline(JsVar *var) {
  if (!var) return;
  do { if (!(jsvGetLocks(var)>0)) jsAssertFail("bin/espruino_embedded.c",2501,""); } while(0);
  if ((var->flags & JSV_LOCK_MASK)==JSV_LOCK_MASK) return;
  JsVarFlags f = var->flags -= JSV_LOCK_ONE;
  if ((f & JSV_LOCK_MASK) == 0) jsvUnLockFreeIfNeeded(var);
}
void jsvUnLock(JsVar *var) {
  jsvUnLockInline(var);
}
void jsvUnLock2(JsVar *var1, JsVar *var2) {
  jsvUnLockInline(var1);
  jsvUnLockInline(var2);
}
void jsvUnLock3(JsVar *var1, JsVar *var2, JsVar *var3) {
  jsvUnLockInline(var1);
  jsvUnLockInline(var2);
  jsvUnLockInline(var3);
}
void jsvUnLock4(JsVar *var1, JsVar *var2, JsVar *var3, JsVar *var4) {
  jsvUnLockInline(var1);
  jsvUnLockInline(var2);
  jsvUnLockInline(var3);
  jsvUnLockInline(var4);
}
__attribute__ ((noinline)) void jsvUnLockMany(unsigned int count, JsVar **vars) {
  while (count) jsvUnLockInline(vars[--count]);
}
JsVar *jsvRef(JsVar *var) {
  do { if (!(var && jsvHasRef(var))) jsAssertFail("bin/espruino_embedded.c",2543,""); } while(0);
  if (jsvGetRefs(var) < ((1<<8)-1))
    jsvSetRefs(var, (JsVarRefCounter)(jsvGetRefs(var)+1));
  do { if (!(jsvGetRefs(var))) jsAssertFail("bin/espruino_embedded.c",2546,""); } while(0);
  return var;
}
void jsvUnRef(JsVar *var) {
  do { if (!(var && jsvGetRefs(var)>0 && jsvHasRef(var))) jsAssertFail("bin/espruino_embedded.c",2552,""); } while(0);
  JsVarRefCounter refs = jsvGetRefs(var);
  if (refs < ((1<<8)-1)) {
    refs--;
    jsvSetRefs(var, refs);
    if (!refs && !jsvGetLocks(var))
      jsvUnLockFreeIfNeeded(var);
  }
}
JsVarRef jsvRefRef(JsVarRef ref) {
  JsVar *v;
  do { if (!(ref)) jsAssertFail("bin/espruino_embedded.c",2566,""); } while(0);
  v = jsvLock(ref);
  do { if (!(!jsvIsStringExt(v))) jsAssertFail("bin/espruino_embedded.c",2568,""); } while(0);
  jsvRef(v);
  jsvUnLock(v);
  return ref;
}
JsVarRef jsvUnRefRef(JsVarRef ref) {
  JsVar *v;
  do { if (!(ref)) jsAssertFail("bin/espruino_embedded.c",2577,""); } while(0);
  v = jsvLock(ref);
  do { if (!(!jsvIsStringExt(v))) jsAssertFail("bin/espruino_embedded.c",2579,""); } while(0);
  jsvUnRef(v);
  jsvUnLock(v);
  return 0;
}
JsVar *jsvNewFlatStringOfLength(unsigned int byteLength) {
  bool firstRun = true;
  size_t requiredBlocks = 1 + ((byteLength+sizeof(JsVar)-1) / sizeof(JsVar));
  JsVar *flatString = 0;
  if (isMemoryBusy) {
    jsErrorFlags |= JSERR_MEMORY_BUSY;
    return 0;
  }
  while (true) {
    bool memoryTouched = true;
    while (memoryTouched) {
      memoryTouched = false;
      touchedFreeList = false;
      JsVarRef beforeStartBlock = 0;
      JsVarRef curr = jsVarFirstEmpty;
      JsVarRef startBlock = curr;
      unsigned int blockCount = 0;
      while (curr && !touchedFreeList) {
        JsVar *currVar = jsvGetAddressOf(curr);
        JsVarRef next = jsvGetNextSibling(currVar);
        if (blockCount && (next == curr+1)) {
          blockCount++;
          if (blockCount>=requiredBlocks) {
            JsVar *nextVar = jsvGetAddressOf(next);
            JsVarRef nextFree = jsvGetNextSibling(nextVar);
            jshInterruptOff();
            if (!touchedFreeList) {
              if (beforeStartBlock) {
                jsvSetNextSibling(jsvGetAddressOf(beforeStartBlock),nextFree);
              } else {
                jsVarFirstEmpty = nextFree;
              }
              flatString = jsvGetAddressOf(startBlock);
              jsvResetVariable(flatString, JSV_FLAT_STRING);
              flatString->varData.integer = (JsVarInt)byteLength;
            }
            jshInterruptOn();
            if (flatString) break;
          }
        } else {
          beforeStartBlock = curr;
          startBlock = next;
          if (startBlock==jsVarsSize || ((size_t)(jsvGetAddressOf(startBlock+1)))&3)
            blockCount = 0;
          else
            blockCount = 1;
        }
        curr = next;
      }
      if (touchedFreeList) {
        memoryTouched = true;
      }
    }
    if (flatString || !firstRun)
      break;
    firstRun = false;
    jsvGarbageCollect();
  };
  if (!flatString) return 0;
  memset((char*)&flatString[1], 0, sizeof(JsVar)*(requiredBlocks-1));
  touchedFreeList = true;
  return flatString;
}
static JsVar *jsvNewNameOrString(const char *str, bool isName) {
  JsVar *first = jsvNewWithFlags(isName ? JSV_NAME_STRING_0 : JSV_STRING_0);
  if (!first) return 0;
  JsVar *var = jsvLockAgain(first);
  while (*str) {
    size_t i, l = jsvGetMaxCharactersInVar(var);
    for (i=0;i<l && *str;i++)
      var->varData.str[i] = *(str++);
    jsvSetCharactersInVar(var, i);
    if (*str) {
      JsVar *next = jsvNewWithFlags(JSV_STRING_EXT_0);
      if (!next) {
        jsvUnLock(var);
        return first;
      }
      jsvSetLastChild(var, jsvGetRef(next));
      jsvUnLock(var);
      var = next;
    }
  }
  jsvUnLock(var);
  return first;
}
JsVar *jsvNewFromString(const char *str) {
  return jsvNewNameOrString(str, false );
}
JsVar *jsvNewNameFromString(const char *str) {
  return jsvNewNameOrString(str, true );
}
JsVar *jsvNewStringOfLength(unsigned int byteLength, const char *initialData) {
  if (byteLength > ((4 + ((14*3 + 0)>>3)) + (4 + ((14*3 + 0 + 8)>>3)))) {
    JsVar *v = jsvNewFlatStringOfLength(byteLength);
    if (v) {
      if (initialData) jsvSetString(v, initialData, byteLength);
      return v;
    }
  }
  JsVar *first = jsvNewWithFlags(JSV_STRING_0);
  if (!first) return 0;
  JsVar *var = jsvLockAgain(first);
  while (true) {
    unsigned int l = (unsigned int)jsvGetMaxCharactersInVar(var);
    if (l>=byteLength) {
      if (initialData)
        memcpy(var->varData.str, initialData, byteLength);
      jsvSetCharactersInVar(var, byteLength);
      break;
    } else {
      if (initialData) {
        memcpy(var->varData.str, initialData, l);
        initialData+=l;
      }
      jsvSetCharactersInVar(var, l);
      byteLength -= l;
      JsVar *next = jsvNewWithFlags(JSV_STRING_EXT_0);
      if (!next) break;
      jsvSetLastChild(var, jsvGetRef(next));
      jsvUnLock(var);
      var = next;
    }
  }
  jsvUnLock(var);
  return first;
}
JsVar *jsvNewFromInteger(JsVarInt value) {
  JsVar *var = jsvNewWithFlags(JSV_INTEGER);
  if (!var) return 0;
  var->varData.integer = value;
  return var;
}
JsVar *jsvNewFromBool(bool value) {
  JsVar *var = jsvNewWithFlags(JSV_BOOLEAN);
  if (!var) return 0;
  var->varData.integer = value ? 1 : 0;
  return var;
}
JsVar *jsvNewFromFloat(JsVarFloat value) {
  JsVar *var = jsvNewWithFlags(JSV_FLOAT);
  if (!var) return 0;
  var->varData.floating = value;
  return var;
}
JsVar *jsvNewFromLongInteger(long long value) {
  if (value>=-2147483648LL && value<=2147483647LL)
    return jsvNewFromInteger((JsVarInt)value);
  else
    return jsvNewFromFloat((JsVarFloat)value);
}
JsVar *jsvNewObject() {
  return jsvNewWithFlags(JSV_OBJECT);
}
JsVar *jsvNewEmptyArray() {
  return jsvNewWithFlags(JSV_ARRAY);
}
JsVar *jsvNewArray(JsVar **elements, int elementCount) {
  JsVar *arr = jsvNewEmptyArray();
  if (!arr) return 0;
  int i;
  for (i=0;i<elementCount;i++)
    jsvArrayPush(arr, elements[i]);
  return arr;
}
JsVar *jsvNewArrayFromBytes(uint8_t *elements, int elementCount) {
  JsVar *arr = jsvNewEmptyArray();
  if (!arr) return 0;
  int i;
  for (i=0;i<elementCount;i++)
    jsvArrayPushAndUnLock(arr, jsvNewFromInteger(elements[i]));
  return arr;
}
JsVar *jsvNewNativeFunction(void (*ptr)(void), unsigned short argTypes) {
  JsVar *func = jsvNewWithFlags(JSV_NATIVE_FUNCTION);
  if (!func) return 0;
  func->varData.native.ptr = ptr;
  func->varData.native.argTypes = argTypes;
  return func;
}
JsVar *jsvNewNativeString(char *ptr, size_t len) {
  if (len>0xFFFFFFFF) len=0xFFFFFFFF;
  JsVar *str = jsvNewWithFlags(JSV_NATIVE_STRING);
  if (!str) return 0;
  if (len > 0xFFFFFFFF)
    len = 0xFFFFFFFF;
  str->varData.nativeStr.ptr = ptr;
  str->varData.nativeStr.len = (JsVarDataNativeStrLength)len;
  return str;
}
JsVar *jsvNewArrayBufferFromString(JsVar *str, unsigned int lengthOrZero) {
  JsVar *arr = jsvNewWithFlags(JSV_ARRAYBUFFER);
  if (!arr) return 0;
  jsvSetFirstChild(arr, jsvGetRef(jsvRef(str)));
  arr->varData.arraybuffer.type = ARRAYBUFFERVIEW_ARRAYBUFFER;
  do { if (!(arr->varData.arraybuffer.byteOffset == 0)) jsAssertFail("bin/espruino_embedded.c",2886,""); } while(0);
  if (lengthOrZero==0) lengthOrZero = (unsigned int)jsvGetStringLength(str);
  arr->varData.arraybuffer.length = (JsVarArrayBufferLength)lengthOrZero;
  return arr;
}
JsVar *jsvMakeIntoVariableName(JsVar *var, JsVar *valueOrZero) {
  if (!var) return 0;
  do { if (!(jsvGetRefs(var)==0)) jsAssertFail("bin/espruino_embedded.c",2894,""); } while(0);
  do { if (!(jsvIsSimpleInt(var) || jsvIsString(var))) jsAssertFail("bin/espruino_embedded.c",2895,""); } while(0);
  JsVarFlags varType = (var->flags & JSV_VARTYPEMASK);
  if (varType==JSV_INTEGER) {
    int t = JSV_NAME_INT;
    if ((jsvIsInt(valueOrZero) || jsvIsBoolean(valueOrZero)) && !jsvIsPin(valueOrZero)) {
      JsVarInt v = valueOrZero->varData.integer;
      if (v>=(-(1<<(14 -1))) && v<=((1<<(14 -1))-1)) {
        t = jsvIsInt(valueOrZero) ? JSV_NAME_INT_INT : JSV_NAME_INT_BOOL;
        jsvSetFirstChild(var, (JsVarRef)v);
        valueOrZero = 0;
      }
    }
    var->flags = (JsVarFlags)(var->flags & ~JSV_VARTYPEMASK) | t;
  } else if (((varType)>=_JSV_STRING_START && (varType)<=_JSV_STRING_END)) {
    if (((varType)==JSV_FLAT_STRING || (varType)==JSV_NATIVE_STRING || false)) {
      JsVar *name = jsvNewWithFlags(JSV_NAME_STRING_0);
      jsvAppendStringVarComplete(name, var);
      jsvUnLock(var);
      var = name;
    } else if (jsvGetCharactersInVar(var) > 4) {
      JsvStringIterator it;
      char queue[(4 + ((14*3 + 0)>>3)) - 4];
      int index;
      jsvStringIteratorNew(&it, var, 4);
      for (index = 0; index < (int)sizeof(queue) && jsvStringIteratorHasChar(&it); index++) {
        queue[index] = jsvStringIteratorGetCharAndNext(&it);
      }
      jsvStringIteratorFree(&it);
      JsVar* last = var;
      while (jsvGetLastChild(last)) last = jsvGetAddressOf(jsvGetLastChild(last));
      if (last != var) {
        int nChars = (int)jsvGetCharactersInVar(last) + index;
        if (nChars <= (4 + ((14*3 + 0 + 8)>>3))) {
          jsvSetCharactersInVar(last, (size_t)nChars);
          last = 0;
        } else {
          index = nChars - (4 + ((14*3 + 0 + 8)>>3));
        }
      }
      if (last) {
        jsvSetCharactersInVar(last, jsvGetMaxCharactersInVar(last));
        JsVar* ext = jsvNewWithFlags(JSV_STRING_EXT_0);
        if (ext) {
          jsvSetCharactersInVar(ext, (size_t)index);
          jsvSetLastChild(last, jsvGetRef(ext));
          jsvUnLock(ext);
        }
      }
      jsvStringIteratorNew(&it, var, (4 + ((14*3 + 0)>>3)));
      do { if (!(it.var == jsvGetAddressOf(jsvGetLastChild(var)) && it.charIdx == 0)) jsAssertFail("bin/espruino_embedded.c",2958,""); } while(0);
      index = 0;
      while (jsvStringIteratorHasChar(&it)) {
        char c = jsvStringIteratorGetChar(&it);
        jsvStringIteratorSetChar(&it, queue[index]);
        queue[index] = c;
        jsvStringIteratorNext(&it);
        index = (index + 1) % (int)sizeof(queue);
      }
      jsvStringIteratorFree(&it);
      jsvSetCharactersInVar(var, 4);
      jsvSetNextSibling(var, 0);
      jsvSetPrevSibling(var, 0);
      jsvSetFirstChild(var, 0);
    }
    size_t t = JSV_NAME_STRING_0;
    if (jsvIsInt(valueOrZero) && !jsvIsPin(valueOrZero)) {
      JsVarInt v = valueOrZero->varData.integer;
      if (v>=(-(1<<(14 -1))) && v<=((1<<(14 -1))-1)) {
        t = JSV_NAME_STRING_INT_0;
        jsvSetFirstChild(var, (JsVarRef)v);
        valueOrZero = 0;
      }
    } else
      jsvSetFirstChild(var, 0);
    var->flags = (var->flags & (JsVarFlags)~JSV_VARTYPEMASK) | (t+jsvGetCharactersInVar(var));
  } else do { if (!(0)) jsAssertFail("bin/espruino_embedded.c",2986,""); } while(0);
  if (valueOrZero)
    jsvSetFirstChild(var, jsvGetRef(jsvRef(valueOrZero)));
  return var;
}
JsVar *jsvMakeFunctionParameter(JsVar *v) {
  do { if (!(jsvIsString(v))) jsAssertFail("bin/espruino_embedded.c",2994,""); } while(0);
  if (!jsvIsName(v))
    v = jsvMakeIntoVariableName(v,0);
  v->flags = (JsVarFlags)(v->flags | JSV_NATIVE);
  return v;
}
void jsvAddFunctionParameter(JsVar *fn, JsVar *paramName, JsVar *value) {
  do { if (!(jsvIsFunction(fn))) jsAssertFail("bin/espruino_embedded.c",3003,""); } while(0);
  if (!paramName) paramName = jsvNewFromEmptyString();
  do { if (!(jsvIsString(paramName))) jsAssertFail("bin/espruino_embedded.c",3005,""); } while(0);
  if (paramName) {
    paramName = jsvMakeFunctionParameter(paramName);
    jsvSetValueOfName(paramName, value);
    jsvAddName(fn, paramName);
    jsvUnLock(paramName);
  }
}
void *jsvGetNativeFunctionPtr(const JsVar *function) {
  JsVar *flatString = jsvFindChildFromString((JsVar*)function, "\xFF""cod");
  if (flatString) {
    flatString = jsvSkipNameAndUnLock(flatString);
    void *v = (void*)((size_t)function->varData.native.ptr + (char*)jsvGetFlatStringPointer(flatString));
    jsvUnLock(flatString);
    return v;
  } else
    return (void *)function->varData.native.ptr;
}
bool jsvIsBasicVarEqual(JsVar *a, JsVar *b) {
  if (a==b) return true;
  if (!a || !b) return false;
  do { if (!(jsvIsBasic(a) && jsvIsBasic(b))) jsAssertFail("bin/espruino_embedded.c",3033,""); } while(0);
  if (jsvIsNumeric(a) && jsvIsNumeric(b)) {
    if (jsvIsIntegerish(a)) {
      if (jsvIsIntegerish(b)) {
        return a->varData.integer == b->varData.integer;
      } else {
        do { if (!(jsvIsFloat(b))) jsAssertFail("bin/espruino_embedded.c",3039,""); } while(0);
        return a->varData.integer == b->varData.floating;
      }
    } else {
      do { if (!(jsvIsFloat(a))) jsAssertFail("bin/espruino_embedded.c",3043,""); } while(0);
      if (jsvIsIntegerish(b)) {
        return a->varData.floating == b->varData.integer;
      } else {
        do { if (!(jsvIsFloat(b))) jsAssertFail("bin/espruino_embedded.c",3047,""); } while(0);
        return a->varData.floating == b->varData.floating;
      }
    }
  } else if (jsvIsString(a) && jsvIsString(b)) {
    JsvStringIterator ita, itb;
    jsvStringIteratorNew(&ita, a, 0);
    jsvStringIteratorNew(&itb, b, 0);
    while (true) {
      int a = jsvStringIteratorGetCharOrMinusOne(&ita);
      jsvStringIteratorNext(&ita);
      int b = jsvStringIteratorGetCharOrMinusOne(&itb);
      jsvStringIteratorNext(&itb);
      if (a != b) {
        jsvStringIteratorFree(&ita);
        jsvStringIteratorFree(&itb);
        return false;
      }
      if (a < 0) {
        jsvStringIteratorFree(&ita);
        jsvStringIteratorFree(&itb);
        return true;
      }
    }
    return false;
  } else {
    return false;
  }
}
bool jsvIsEqual(JsVar *a, JsVar *b) {
  if (jsvIsBasic(a) && jsvIsBasic(b))
    return jsvIsBasicVarEqual(a,b);
  return jsvGetRef(a)==jsvGetRef(b);
}
const char *jsvGetConstString(const JsVar *v) {
  if (jsvIsUndefined(v)) {
    return "undefined";
  } else if (jsvIsNull(v)) {
    return "null";
  } else if (jsvIsBoolean(v) && !jsvIsNameIntBool(v)) {
    return jsvGetBool(v) ? "true" : "false";
  }
  return 0;
}
const char *jsvGetTypeOf(const JsVar *v) {
  if (jsvIsUndefined(v)) return "undefined";
  if (jsvIsNull(v) || jsvIsObject(v) ||
      jsvIsArray(v) || jsvIsArrayBuffer(v)) return "object";
  if (jsvIsFunction(v)) return "function";
  if (jsvIsString(v)) return "string";
  if (jsvIsBoolean(v)) return "boolean";
  if (jsvIsNumeric(v)) return "number";
  return "?";
}
JsVar *jsvGetValueOf(JsVar *v) {
  if (!jsvIsObject(v)) return jsvLockAgainSafe(v);
  JsVar *valueOf = jspGetNamedField(v, "valueOf", false);
  if (!jsvIsFunction(valueOf)) {
    jsvUnLock(valueOf);
    return jsvLockAgain(v);
  }
  v = jspeFunctionCall(valueOf, 0, v, false, 0, 0);
  jsvUnLock(valueOf);
  return v;
}
size_t jsvGetString(const JsVar *v, char *str, size_t len) {
  do { if (!(len>0)) jsAssertFail("bin/espruino_embedded.c",3126,""); } while(0);
  const char *s = jsvGetConstString(v);
  if (s) {
    len--;
    size_t l = 0;
    while (s[l] && l<len) {
      str[l] = s[l];
      l++;
    }
    str[l] = 0;
    return l;
  } else if (jsvIsInt(v)) {
    itostr(v->varData.integer, str, 10);
    return strlen(str);
  } else if (jsvIsFloat(v)) {
    ftoa_bounded(v->varData.floating, str, len);
    return strlen(str);
  } else if (jsvHasCharacterData(v)) {
    do { if (!(!jsvIsStringExt(v))) jsAssertFail("bin/espruino_embedded.c",3146,""); } while(0);
    size_t l = len;
    JsvStringIterator it;
    jsvStringIteratorNew(&it, (JsVar*)v, 0);
    while (jsvStringIteratorHasChar(&it)) {
      if (l--<=1) {
        *str = 0;
        jsvStringIteratorFree(&it);
        return len;
      }
      *(str++) = jsvStringIteratorGetChar(&it);
      jsvStringIteratorNext(&it);
    }
    jsvStringIteratorFree(&it);
    *str = 0;
    return len-l;
  } else {
    JsVar *stringVar = jsvAsString((JsVar*)v);
    if (stringVar) {
      size_t l = jsvGetStringChars(stringVar, 0, str, len);
      jsvUnLock(stringVar);
      if (l>=len) l=len-1;
      str[l] = 0;
      return l;
    } else {
      str[0] = 0;
      jsExceptionHere(JSET_INTERNALERROR, "Variable type cannot be converted to string");
      return 0;
    }
  }
}
size_t jsvGetStringChars(const JsVar *v, size_t startChar, char *str, size_t len) {
  do { if (!(jsvHasCharacterData(v))) jsAssertFail("bin/espruino_embedded.c",3181,""); } while(0);
  size_t l = len;
  JsvStringIterator it;
  jsvStringIteratorNew(&it, (JsVar*)v, startChar);
  while (jsvStringIteratorHasChar(&it)) {
    if (l--<=0) {
      jsvStringIteratorFree(&it);
      return len;
    }
    *(str++) = jsvStringIteratorGetCharAndNext(&it);
  }
  jsvStringIteratorFree(&it);
  return len-l;
}
void jsvSetString(JsVar *v, const char *str, size_t len) {
  do { if (!(jsvHasCharacterData(v))) jsAssertFail("bin/espruino_embedded.c",3198,""); } while(0);
  JsvStringIterator it;
  jsvStringIteratorNew(&it, v, 0);
  size_t i;
  for (i=0;i<len;i++) {
    jsvStringIteratorSetCharAndNext(&it, str[i]);
  }
  jsvStringIteratorFree(&it);
}
JsVar *jsvAsString(JsVar *v) {
  JsVar *str = 0;
  if (jsvHasCharacterData(v) && jsvIsName(v)) {
    str = jsvNewFromStringVarComplete(v);
  } else if (jsvIsString(v)) {
    str = jsvLockAgain(v);
  } else if (jsvIsObject(v)) {
    JsVar *toStringFn = jspGetNamedField(v, "toString", false);
    if (toStringFn && toStringFn->varData.native.ptr != (void (*)(void))jswrap_object_toString) {
      JsVar *result = jspExecuteFunction(toStringFn,v,0,0);
      jsvUnLock(toStringFn);
      str = jsvAsStringAndUnLock(result);
    } else {
      jsvUnLock(toStringFn);
      str = jsvNewFromString("[object Object]");
    }
  } else {
    const char *constChar = jsvGetConstString(v);
    do { if (!(70>=10)) jsAssertFail("bin/espruino_embedded.c",3234,""); } while(0);
    char buf[70];
    if (constChar) {
      str = jsvNewFromString(constChar);
    } else if (jsvIsInt(v)) {
      itostr(v->varData.integer, buf, 10);
      str = jsvNewFromString(buf);
    } else if (jsvIsFloat(v)) {
      ftoa_bounded(v->varData.floating, buf, sizeof(buf));
      str = jsvNewFromString(buf);
    } else if (jsvIsArray(v) || jsvIsArrayBuffer(v)) {
      JsVar *filler = jsvNewFromString(",");
      str = jsvArrayJoin(v, filler, true );
      jsvUnLock(filler);
    } else if (jsvIsFunction(v)) {
      str = jsvNewFromEmptyString();
      if (str) jsfGetJSON(v, str, JSON_NONE);
    } else {
      jsExceptionHere(JSET_INTERNALERROR, "Variable type cannot be converted to string");
    }
  }
  return str;
}
JsVar *jsvAsStringAndUnLock(JsVar *var) {
  JsVar *s = jsvAsString(var);
  jsvUnLock(var);
  return s;
}
JsVar *jsvAsFlatString(JsVar *var) {
  if (jsvIsFlatString(var)) return jsvLockAgain(var);
  JsVar *str = jsvAsString(var);
  JsVar *fs = jsvNewFlatStringFromStringVar(str, 0, (0x7FFFFFFF));
  jsvUnLock(str);
  return fs;
}
JsVar *jsvAsArrayIndex(JsVar *index) {
  if (jsvIsSimpleInt(index) && jsvGetInteger(index)>=0) {
    return jsvLockAgain(index);
  } else if (jsvIsString(index)) {
    if (jsvIsStringNumericStrict(index)) {
      JsVar *i = jsvNewFromInteger(jsvGetInteger(index));
      JsVar *is = jsvAsString(i);
      if (jsvCompareString(index,is,0,0,false)==0) {
        jsvUnLock(is);
        return i;
      } else {
        jsvUnLock2(i,is);
      }
    }
    return jsvLockAgain(index);
  } else if (jsvIsFloat(index)) {
    JsVarFloat v = jsvGetFloat(index);
    JsVarInt vi = jsvGetInteger(index);
    if (v == vi) return jsvNewFromInteger(vi);
  }
  return jsvAsString(index);
}
JsVar *jsvAsArrayIndexAndUnLock(JsVar *a) {
  JsVar *b = jsvAsArrayIndex(a);
  jsvUnLock(a);
  return b;
}
bool jsvIsEmptyString(JsVar *v) {
  if (!jsvHasCharacterData(v)) return true;
  return jsvGetCharactersInVar(v)==0;
}
size_t jsvGetStringLength(const JsVar *v) {
  size_t strLength = 0;
  if (jsvIsUTF8String(v)) {
    JsvStringIterator it;
    jsvStringIteratorNew(&it, (JsVar*)v, 0);
    while (jsvStringIteratorHasChar(&it)) {
      jsvStringIteratorNextUTF8(&it);
      strLength++;
    }
    jsvStringIteratorFree(&it);
    return strLength;
  }
  const JsVar *var = v;
  JsVar *newVar = 0;
  if (!jsvHasCharacterData(v)) return 0;
  while (var) {
    JsVarRef ref = jsvGetLastChild(var);
    strLength += jsvGetCharactersInVar(var);
    jsvUnLock(newVar);
    var = newVar = jsvLockSafe(ref);
  }
  jsvUnLock(newVar);
  return strLength;
}
size_t jsvGetFlatStringBlocks(const JsVar *v) {
  do { if (!(jsvIsFlatString(v))) jsAssertFail("bin/espruino_embedded.c",3359,""); } while(0);
  return ((size_t)v->varData.integer+sizeof(JsVar)-1) / sizeof(JsVar);
}
char *jsvGetFlatStringPointer(JsVar *v) {
  do { if (!(jsvIsFlatString(v))) jsAssertFail("bin/espruino_embedded.c",3364,""); } while(0);
  if (!jsvIsFlatString(v)) return 0;
  return (char*)(v+1);
}
JsVar *jsvGetFlatStringFromPointer(char *v) {
  JsVar *secondVar = (JsVar*)v;
  JsVar *flatStr = secondVar-1;
  do { if (!(jsvIsFlatString(flatStr))) jsAssertFail("bin/espruino_embedded.c",3372,""); } while(0);
  return flatStr;
}
char *jsvGetDataPointer(JsVar *v, size_t *len) {
  do { if (!(len)) jsAssertFail("bin/espruino_embedded.c",3378,""); } while(0);
  if (jsvIsArrayBuffer(v)) {
    JsVar *d = jsvGetArrayBufferBackingString(v, NULL);
    char *r = jsvGetDataPointer(d, len);
    jsvUnLock(d);
    if (r) {
      r += v->varData.arraybuffer.byteOffset;
      *len = v->varData.arraybuffer.length;
    }
    return r;
  }
  if (jsvIsNativeString(v)) {
    *len = v->varData.nativeStr.len;
    return (char*)v->varData.nativeStr.ptr;
  }
  if (jsvIsFlatString(v)) {
    *len = jsvGetStringLength(v);
    return jsvGetFlatStringPointer(v);
  }
  if (jsvIsBasicString(v) && !jsvGetLastChild(v)) {
    *len = jsvGetCharactersInVar(v);
    return (char*)v->varData.str;
  }
  return 0;
}
size_t jsvGetLinesInString(JsVar *v) {
  size_t lines = 1;
  JsvStringIterator it;
  jsvStringIteratorNew(&it, v, 0);
  while (jsvStringIteratorHasChar(&it)) {
    if (jsvStringIteratorGetCharAndNext(&it)=='\n') lines++;
  }
  jsvStringIteratorFree(&it);
  return lines;
}
size_t jsvGetCharsOnLine(JsVar *v, size_t line) {
  size_t currentLine = 1;
  size_t chars = 0;
  JsvStringIterator it;
  jsvStringIteratorNew(&it, v, 0);
  while (jsvStringIteratorHasChar(&it)) {
    if (jsvStringIteratorGetCharAndNext(&it)=='\n') {
      currentLine++;
      if (currentLine > line) break;
    } else if (currentLine==line) chars++;
  }
  jsvStringIteratorFree(&it);
  return chars;
}
void jsvGetLineAndCol(JsVar *v, size_t charIdx, size_t *line, size_t *col, size_t *ignoredLines) {
  size_t x = 1;
  size_t y = 1;
  size_t n = 0;
  do { if (!(line && col)) jsAssertFail("bin/espruino_embedded.c",3443,""); } while(0);
  const char *ignoreLine = "Modules.addCached";
  int ignoreLineIdx = 0;
  if (ignoredLines) *ignoredLines=0;
  JsvStringIterator it;
  jsvStringIteratorNew(&it, v, 0);
  while (jsvStringIteratorHasChar(&it)) {
    char ch = jsvStringIteratorGetCharAndNext(&it);
    if (ignoredLines && y==1+*ignoredLines && ignoreLineIdx>=0 && ch==ignoreLine[ignoreLineIdx]) {
      ignoreLineIdx++;
      if (ignoreLine[ignoreLineIdx]==0) {
        ignoreLineIdx=-1;
        (*ignoredLines)++;
      }
    }
    if (n==charIdx) {
      jsvStringIteratorFree(&it);
      *line = y;
      *col = x;
      return;
    }
    x++;
    if (ch=='\n') {
      x=1; y++;
      ignoreLineIdx = 0;
    }
    n++;
  }
  jsvStringIteratorFree(&it);
  *line = y;
  *col = x;
}
size_t jsvGetIndexFromLineAndCol(JsVar *v, size_t line, size_t col) {
  size_t x = 1;
  size_t y = 1;
  size_t n = 0;
  JsvStringIterator it;
  jsvStringIteratorNew(&it, v, 0);
  while (jsvStringIteratorHasChar(&it)) {
    char ch = jsvStringIteratorGetCharAndNext(&it);
    if ((y==line && x>=col) || y>line) {
      jsvStringIteratorFree(&it);
      return (y>line) ? (n-1) : n;
    }
    x++;
    if (ch=='\n') {
      x=1; y++;
    }
    n++;
  }
  jsvStringIteratorFree(&it);
  return n;
}
void jsvAppendString(JsVar *var, const char *str) {
  do { if (!(jsvIsString(var))) jsAssertFail("bin/espruino_embedded.c",3507,""); } while(0);
  JsvStringIterator dst;
  jsvStringIteratorNew(&dst, var, 0);
  jsvStringIteratorGotoEnd(&dst);
  while (*str)
    jsvStringIteratorAppend(&dst, *(str++));
  jsvStringIteratorFree(&dst);
}
void jsvAppendStringBuf(JsVar *var, const char *str, size_t length) {
  do { if (!(jsvIsString(var))) jsAssertFail("bin/espruino_embedded.c",3521,""); } while(0);
  JsvStringIterator dst;
  jsvStringIteratorNew(&dst, var, 0);
  jsvStringIteratorGotoEnd(&dst);
  while (length) {
    jsvStringIteratorAppend(&dst, *(str++));
    length--;
  }
  jsvStringIteratorFree(&dst);
}
void jsvStringIteratorPrintfCallback(const char *str, void *user_data) {
  while (*str)
    jsvStringIteratorAppend((JsvStringIterator *)user_data, *(str++));
}
void jsvAppendPrintf(JsVar *var, const char *fmt, ...) {
  JsvStringIterator it;
  jsvStringIteratorNew(&it, var, 0);
  jsvStringIteratorGotoEnd(&it);
  va_list argp;
  va_start(argp, fmt);
  vcbprintf((vcbprintf_callback)jsvStringIteratorPrintfCallback,&it, fmt, argp);
  va_end(argp);
  jsvStringIteratorFree(&it);
}
JsVar *jsvVarPrintf( const char *fmt, ...) {
  JsVar *str = jsvNewFromEmptyString();
  if (!str) return 0;
  JsvStringIterator it;
  jsvStringIteratorNew(&it, str, 0);
  jsvStringIteratorGotoEnd(&it);
  va_list argp;
  va_start(argp, fmt);
  vcbprintf((vcbprintf_callback)jsvStringIteratorPrintfCallback,&it, fmt, argp);
  va_end(argp);
  jsvStringIteratorFree(&it);
  return str;
}
void jsvAppendStringVar(JsVar *var, const JsVar *str, size_t stridx, size_t maxLength) {
  do { if (!(jsvIsString(var))) jsAssertFail("bin/espruino_embedded.c",3572,""); } while(0);
  JsvStringIterator dst;
  jsvStringIteratorNew(&dst, var, 0);
  jsvStringIteratorGotoEnd(&dst);
  JsvStringIterator it;
  jsvStringIteratorNew(&it, (JsVar*)str, stridx);
  while (jsvStringIteratorHasChar(&it) && (maxLength-->0)) {
    char ch = jsvStringIteratorGetCharAndNext(&it);
    jsvStringIteratorAppend(&dst, ch);
  }
  jsvStringIteratorFree(&it);
  jsvStringIteratorFree(&dst);
}
JsVar *jsvNewFlatStringFromStringVar(JsVar *var, size_t stridx, size_t maxLength) {
  do { if (!(jsvIsString(var))) jsAssertFail("bin/espruino_embedded.c",3592,""); } while(0);
  size_t len = jsvGetStringLength(var);
  if (stridx>len) len=0;
  else len -= stridx;
  if (len > maxLength) len = maxLength;
  JsVar *flat = jsvNewFlatStringOfLength((unsigned int)len);
  if (flat) {
    JsvStringIterator src;
    JsvStringIterator dst;
    jsvStringIteratorNew(&src, var, stridx);
    jsvStringIteratorNew(&dst, flat, 0);
    while (len--) {
      jsvStringIteratorSetCharAndNext(&dst, jsvStringIteratorGetCharAndNext(&src));
    }
    jsvStringIteratorFree(&src);
    jsvStringIteratorFree(&dst);
  }
  return flat;
}
JsVar *jsvNewWritableStringFromStringVar(const JsVar *str, size_t stridx, size_t maxLength) {
  JsVar *var = jsvNewFromEmptyString();
  jsvAppendStringVar(var, str, stridx, maxLength);
  return var;
}
JsVar *jsvNewFromStringVar(const JsVar *str, size_t stridx, size_t maxLength) {
  do { if (!(jsvIsString(str))) jsAssertFail("bin/espruino_embedded.c",3627,""); } while(0);
  if (jsvIsNativeString(str) || jsvIsFlashString(str)) {
    size_t l = jsvGetStringLength(str);
    if (stridx>l) stridx=l;
    if (stridx+maxLength>l) maxLength=l-stridx;
    JsVar *res = jsvNewWithFlags(str->flags&JSV_VARTYPEMASK);
    res->varData.nativeStr.ptr = str->varData.nativeStr.ptr + stridx;
    res->varData.nativeStr.len = (JsVarDataNativeStrLength)maxLength;
    return res;
  }
  if (jsvIsFlatString(str)) {
    size_t length = jsvGetCharactersInVar(str);
    if (stridx >= length) length = 0;
    else length -= stridx;
    if (length > maxLength) length = maxLength;
    if (length > ((4 + ((14*3 + 0)>>3)) + (4 + ((14*3 + 0 + 8)>>3)))) {
      JsVar *var = jsvNewFlatStringFromStringVar(str, stridx, length);
      if (var) return var;
    }
  }
  return jsvNewWritableStringFromStringVar(str, stridx, maxLength);
}
JsVar *jsvNewFromStringVarComplete(JsVar *var) {
  return jsvNewWritableStringFromStringVar(var, 0, (0x7FFFFFFF));
}
void jsvAppendStringVarComplete(JsVar *var, const JsVar *str) {
  jsvAppendStringVar(var, str, 0, (0x7FFFFFFF));
}
int jsvGetCharInString(JsVar *v, size_t idx) {
  if (!jsvIsString(v)) return 0;
  JsvStringIterator it;
  jsvStringIteratorNewUTF8(&it, v, idx);
  int ch = jsvStringIteratorGetUTF8CharAndNext(&it);
  jsvStringIteratorFree(&it);
  return ch;
}
void jsvSetCharInString(JsVar *v, size_t idx, char ch, bool bitwiseOR) {
  if (!jsvIsString(v)) return;
  JsvStringIterator it;
  jsvStringIteratorNew(&it, v, idx);
  if (bitwiseOR) ch |= jsvStringIteratorGetChar(&it);
  jsvStringIteratorSetChar(&it, ch);
  jsvStringIteratorFree(&it);
}
int jsvGetStringIndexOf(JsVar *str, char ch) {
  JsvStringIterator it;
  jsvStringIteratorNew(&it, str, 0);
  while (jsvStringIteratorHasChar(&it)) {
    if (jsvStringIteratorGetChar(&it) == ch) {
      int idx = (int)jsvStringIteratorGetIndex(&it);
      jsvStringIteratorFree(&it);
      return idx;
    };
    jsvStringIteratorNext(&it);
  }
  jsvStringIteratorFree(&it);
  return -1;
}
int jsvConvertFromUTF8Index(JsVar *str, int idx) {
  return idx;
}
int jsvConvertToUTF8Index(JsVar *str, int idx) {
  return idx;
}
bool jsvIsStringNumericInt(const JsVar *var, bool allowDecimalPoint) {
  do { if (!(jsvIsString(var))) jsAssertFail("bin/espruino_embedded.c",3767,""); } while(0);
  JsvStringIterator it;
  jsvStringIteratorNew(&it, (JsVar*)var, 0);
  while (jsvStringIteratorHasChar(&it) && isWhitespace(jsvStringIteratorGetChar(&it)))
    jsvStringIteratorNext(&it);
  if (jsvStringIteratorGetChar(&it)=='-' || jsvStringIteratorGetChar(&it)=='+')
    jsvStringIteratorNext(&it);
  int radix = 0;
  if (jsvStringIteratorGetChar(&it)=='0') {
    jsvStringIteratorNext(&it);
    char buf[3];
    buf[0] = '0';
    buf[1] = jsvStringIteratorGetChar(&it);
    buf[2] = 0;
    const char *p = buf;
    radix = getRadix(&p);
    if (p>&buf[1]) jsvStringIteratorNext(&it);
  }
  if (radix==0) radix=10;
  int chars=0;
  while (jsvStringIteratorHasChar(&it)) {
    chars++;
    char ch = jsvStringIteratorGetCharAndNext(&it);
    if (ch=='.' && allowDecimalPoint) {
      allowDecimalPoint = false;
    } else {
      int n = chtod(ch);
      if (n<0 || n>=radix) {
        jsvStringIteratorFree(&it);
        return false;
      }
    }
  }
  jsvStringIteratorFree(&it);
  return chars>0;
}
bool jsvIsStringNumericStrict(const JsVar *var) {
  do { if (!(jsvIsString(var))) jsAssertFail("bin/espruino_embedded.c",3814,""); } while(0);
  JsvStringIterator it;
  jsvStringIteratorNew(&it, (JsVar*)var, 0);
  bool hadNonZero = false;
  bool hasLeadingZero = false;
  int chars = 0;
  while (jsvStringIteratorHasChar(&it)) {
    chars++;
    char ch = jsvStringIteratorGetCharAndNext(&it);
    if (!isNumeric(ch)) {
      jsvStringIteratorFree(&it);
      return false;
    }
    if (!hadNonZero && ch=='0') hasLeadingZero=true;
    if (ch!='0') hadNonZero=true;
  }
  jsvStringIteratorFree(&it);
  return chars>0 && (!hasLeadingZero || chars==1);
}
JsVarInt jsvGetInteger(const JsVar *v) {
  if (!v) return 0;
  if (jsvIsNull(v)) return 0;
  if (jsvIsUndefined(v)) return 0;
  if (jsvIsIntegerish(v) || jsvIsArrayBufferName(v)) return v->varData.integer;
  if (jsvIsArray(v) || jsvIsArrayBuffer(v)) {
    JsVarInt l = jsvGetLength((JsVar *)v);
    if (l==0) return 0;
    if (l==1) {
      if (jsvIsArrayBuffer(v))
        return jsvGetIntegerAndUnLock(jsvArrayBufferGet((JsVar*)v,0));
      return jsvGetIntegerAndUnLock(jsvSkipNameAndUnLock(jsvGetArrayItem(v,0)));
    }
  }
  if (jsvIsFloat(v)) {
    if (isfinite(v->varData.floating))
      return (JsVarInt)(long long)v->varData.floating;
    return 0;
  }
  if (jsvIsString(v) && jsvIsStringNumericInt(v, true )) {
    char buf[32];
    if (jsvGetString(v, buf, sizeof(buf))==sizeof(buf))
      jsExceptionHere(JSET_ERROR, "String too big to convert to number");
    else
      return (JsVarInt)stringToInt(buf);
  }
  return 0;
}
long long jsvGetLongInteger(const JsVar *v) {
  if (jsvIsInt(v)) return jsvGetInteger(v);
  return (long long)jsvGetFloat(v);
}
long long jsvGetLongIntegerAndUnLock(JsVar *v) {
  long long i = jsvGetLongInteger(v);
  jsvUnLock(v);
  return i;
}
void jsvSetInteger(JsVar *v, JsVarInt value) {
  do { if (!(jsvIsInt(v))) jsAssertFail("bin/espruino_embedded.c",3879,""); } while(0);
  v->varData.integer = value;
}
bool jsvGetBool(const JsVar *v) {
  if (jsvIsString(v))
    return !jsvIsEmptyString((JsVar*)v);
  if (jsvIsFunction(v) || jsvIsArray(v) || jsvIsObject(v) || jsvIsArrayBuffer(v))
    return true;
  if (jsvIsFloat(v)) {
    JsVarFloat f = jsvGetFloat(v);
    return !isnan(f) && f!=0.0;
  }
  return jsvGetInteger(v)!=0;
}
JsVarFloat jsvGetFloat(const JsVar *v) {
  if (!v) return NAN;
  if (jsvIsFloat(v)) return v->varData.floating;
  if (jsvIsIntegerish(v)) return (JsVarFloat)v->varData.integer;
  if (jsvIsArray(v) || jsvIsArrayBuffer(v)) {
    JsVarInt l = jsvGetLength(v);
    if (l==0) return 0;
    if (l==1) {
      if (jsvIsArrayBuffer(v))
        return jsvGetFloatAndUnLock(jsvArrayBufferGet((JsVar*)v,0));
      return jsvGetFloatAndUnLock(jsvSkipNameAndUnLock(jsvGetArrayItem(v,0)));
    }
  }
  if (jsvIsString(v)) {
    char buf[64];
    if (jsvGetString(v, buf, sizeof(buf))==sizeof(buf)) {
      jsExceptionHere(JSET_ERROR, "String too big to convert to number");
    } else {
      if (buf[0]==0) return 0;
      if (!strcmp(buf,"Infinity")) return INFINITY;
      if (!strcmp(buf,"-Infinity")) return -INFINITY;
      const char *endOfNumber = 0;
      JsVarFloat v = stringToFloatWithRadix(buf,0,&endOfNumber);
      if (*endOfNumber==0) return v;
    }
  }
  return NAN;
}
JsVar *jsvAsNumber(JsVar *var) {
  if (jsvIsInt(var) || jsvIsFloat(var)) return jsvLockAgain(var);
  if (jsvIsBoolean(var) ||
      jsvIsPin(var) ||
      jsvIsNull(var) ||
      jsvIsBoolean(var) ||
      jsvIsArrayBufferName(var))
    return jsvNewFromInteger(jsvGetInteger(var));
  if (jsvIsString(var) && (jsvIsEmptyString(var) || jsvIsStringNumericInt(var, false ))) {
    char buf[64];
    if (jsvGetString(var, buf, sizeof(buf))==sizeof(buf)) {
      jsExceptionHere(JSET_ERROR, "String too big to convert to number");
      return jsvNewFromFloat(NAN);
    } else
      return jsvNewFromLongInteger(stringToInt(buf));
  }
  return jsvNewFromFloat(jsvGetFloat(var));
}
JsVar *jsvAsNumberAndUnLock(JsVar *v) { JsVar *n = jsvAsNumber(v); jsvUnLock(v); return n; }
JsVarInt jsvGetIntegerAndUnLock(JsVar *v) { return _jsvGetIntegerAndUnLock(v); }
JsVarFloat jsvGetFloatAndUnLock(JsVar *v) { return _jsvGetFloatAndUnLock(v); }
bool jsvGetBoolAndUnLock(JsVar *v) { return _jsvGetBoolAndUnLock(v); }
JsVar *jsvExecuteGetter(JsVar *parent, JsVar *getset) {
  do { if (!(jsvIsGetterOrSetter(getset))) jsAssertFail("bin/espruino_embedded.c",3971,""); } while(0);
  if (!jsvIsGetterOrSetter(getset)) return 0;
  JsVar *fn = jsvObjectGetChildIfExists(getset, "get");
  if (!jsvIsFunction(fn)) {
    jsvUnLock(fn);
    return 0;
  }
  JsVar *result = jspExecuteFunction(fn, parent, 0, NULL);
  jsvUnLock(fn);
  return result;
}
void jsvExecuteSetter(JsVar *parent, JsVar *getset, JsVar *value) {
  do { if (!(jsvIsGetterOrSetter(getset))) jsAssertFail("bin/espruino_embedded.c",3985,""); } while(0);
  if (!jsvIsGetterOrSetter(getset)) return;
  JsVar *fn = jsvObjectGetChildIfExists(getset, "set");
  if (!jsvIsFunction(fn)) {
    jsvUnLock(fn);
    return;
  }
  if (!fn) return;
  jsvUnLock2(jspExecuteFunction(fn, parent, 1, &value), fn);
}
void jsvAddGetterOrSetter(JsVar *obj, JsVar *varName, bool isGetter, JsVar *method) {
  JsVar *getsetName = jsvFindChildFromVar(obj, varName, true);
  if (jsvIsName(getsetName)) {
    JsVar *getset = jsvGetValueOfName(getsetName);
    if (!jsvIsGetterOrSetter(getset)) {
      jsvUnLock(getset);
      getset = jsvNewWithFlags(JSV_GET_SET);
      jsvSetValueOfName(getsetName, getset);
    }
    if (jsvIsGetterOrSetter(getset))
      jsvObjectSetChild(getset, isGetter?"get":"set", method);
    jsvUnLock(getset);
  }
  jsvUnLock(getsetName);
}
void jsvReplaceWith(JsVar *dst, JsVar *src) {
  if (jsvIsArrayBufferName(dst)) {
    size_t idx = (size_t)jsvGetInteger(dst);
    JsVar *arrayBuffer = jsvLock(jsvGetFirstChild(dst));
    jsvArrayBufferSet(arrayBuffer, idx, src);
    jsvUnLock(arrayBuffer);
    return;
  }
  if (!jsvIsName(dst)) {
    jsExceptionHere(JSET_ERROR, "Unable to assign value to non-reference %t", dst);
    return;
  }
  if (jsvIsConstant(dst)) {
    jsExceptionHere(JSET_TYPEERROR, "Assignment to a constant");
    return;
  }
  JsVar *v = jsvGetValueOfName(dst);
  if (jsvIsGetterOrSetter(v)) {
    JsVar *parent = jsvIsNewChild(dst)?jsvLock(jsvGetNextSibling(dst)):0;
    jsvExecuteSetter(parent,v,src);
    jsvUnLock2(v,parent);
    return;
  }
  jsvUnLock(v);
  jsvSetValueOfName(dst, src);
  if (jsvIsNewChild(dst)) {
    JsVar *parent = jsvLock(jsvGetNextSibling(dst));
    if (!jsvIsString(parent)) {
      if (!jsvHasChildren(parent)) {
        jsExceptionHere(JSET_ERROR, "Field or method %q does not already exist, and can't create it on %t", dst, parent);
      } else {
        jsvUnRef(parent);
        jsvSetNextSibling(dst, 0);
        jsvUnRef(parent);
        jsvSetPrevSibling(dst, 0);
        jsvAddName(parent, dst);
      }
    }
    jsvUnLock(parent);
  }
}
void jsvReplaceWithOrAddToRoot(JsVar *dst, JsVar *src) {
  if (!jsvGetRefs(dst) && jsvIsName(dst)) {
    if (!jsvIsArrayBufferName(dst) && !jsvIsNewChild(dst))
      jsvAddName(execInfo.root, dst);
  }
  jsvReplaceWith(dst, src);
}
size_t jsvGetArrayBufferLength(const JsVar *arrayBuffer) {
  do { if (!(jsvIsArrayBuffer(arrayBuffer))) jsAssertFail("bin/espruino_embedded.c",4091,""); } while(0);
  return arrayBuffer->varData.arraybuffer.length;
}
JsVar *jsvGetArrayBufferBackingString(JsVar *arrayBuffer, uint32_t *offset) {
  if (!arrayBuffer) return 0;
  jsvLockAgain(arrayBuffer);
  if (offset) *offset = 0;
  while (jsvIsArrayBuffer(arrayBuffer)) {
    if (offset) *offset += arrayBuffer->varData.arraybuffer.byteOffset;
    JsVar *s = jsvLock(jsvGetFirstChild(arrayBuffer));
    jsvUnLock(arrayBuffer);
    arrayBuffer = s;
  }
  do { if (!(jsvIsString(arrayBuffer))) jsAssertFail("bin/espruino_embedded.c",4106,""); } while(0);
  return arrayBuffer;
}
JsVar *jsvArrayBufferGet(JsVar *arrayBuffer, size_t idx) {
  JsvArrayBufferIterator it;
  jsvArrayBufferIteratorNew(&it, arrayBuffer, idx);
  JsVar *v = jsvArrayBufferIteratorGetValue(&it, false );
  jsvArrayBufferIteratorFree(&it);
  return v;
}
void jsvArrayBufferSet(JsVar *arrayBuffer, size_t idx, JsVar *value) {
  JsvArrayBufferIterator it;
  jsvArrayBufferIteratorNew(&it, arrayBuffer, idx);
  jsvArrayBufferIteratorSetValue(&it, value, false );
  jsvArrayBufferIteratorFree(&it);
}
JsVar *jsvArrayBufferGetFromName(JsVar *name) {
  do { if (!(jsvIsArrayBufferName(name))) jsAssertFail("bin/espruino_embedded.c",4130,""); } while(0);
  size_t idx = (size_t)jsvGetInteger(name);
  JsVar *arrayBuffer = jsvLock(jsvGetFirstChild(name));
  JsVar *value = jsvArrayBufferGet(arrayBuffer, idx);
  jsvUnLock(arrayBuffer);
  return value;
}
JsVar *jsvGetFunctionArgumentLength(JsVar *functionScope) {
  JsVar *args = jsvNewEmptyArray();
  if (!args) return 0;
  JsvObjectIterator it;
  jsvObjectIteratorNew(&it, functionScope);
  while (jsvObjectIteratorHasValue(&it)) {
    JsVar *idx = jsvObjectIteratorGetKey(&it);
    if (jsvIsFunctionParameter(idx)) {
      JsVar *val = jsvSkipOneName(idx);
      jsvArrayPushAndUnLock(args, val);
    }
    jsvUnLock(idx);
    jsvObjectIteratorNext(&it);
  }
  jsvObjectIteratorFree(&it);
  return args;
}
bool jsvIsVariableDefined(JsVar *a) {
  return !jsvIsName(a) ||
         jsvIsNameWithValue(a) ||
         (jsvGetFirstChild(a)!=0);
}
JsVar *jsvGetValueOfName(JsVar *a) {
  if (!a) return 0;
  JsVarFlags flags = a->flags&JSV_VARTYPEMASK;
  if (flags==JSV_ARRAYBUFFERNAME ) return jsvArrayBufferGetFromName(a);
  if ((flags==JSV_NAME_INT_INT || (flags>=JSV_NAME_STRING_INT_0 && flags<=JSV_NAME_STRING_INT_MAX)) ) return jsvNewFromInteger((JsVarInt)jsvGetFirstChildSigned(a));
  if (flags==JSV_NAME_INT_BOOL ) return jsvNewFromBool(jsvGetFirstChild(a)!=0);
  do { if (!(!jsvIsNameWithValue(a))) jsAssertFail("bin/espruino_embedded.c",4177,""); } while(0);
  if (((flags)>=_JSV_NAME_START && (flags)<=_JSV_NAME_END))
    return jsvLockSafe(jsvGetFirstChild(a));
  return 0;
}
void jsvCheckReferenceError(JsVar *a) {
  if (jsvIsBasicName(a) && jsvGetRefs(a)==0 && !jsvIsNewChild(a) && !jsvGetFirstChild(a))
    jsExceptionHere(JSET_REFERENCEERROR, "%q is not defined", a);
}
JsVar *jsvSkipNameWithParent(JsVar *a, bool repeat, JsVar *parent) {
  if (!a) return 0;
  JsVarFlags flags = a->flags&JSV_VARTYPEMASK;
  if (flags==JSV_ARRAYBUFFERNAME ) return jsvArrayBufferGetFromName(a);
  if ((flags==JSV_NAME_INT_INT || (flags>=JSV_NAME_STRING_INT_0 && flags<=JSV_NAME_STRING_INT_MAX)) ) return jsvNewFromInteger((JsVarInt)jsvGetFirstChildSigned(a));
  if (flags==JSV_NAME_INT_BOOL ) return jsvNewFromBool(jsvGetFirstChild(a)!=0);
  JsVar *pa = jsvLockAgain(a);
  while (((flags)>=_JSV_NAME_START && (flags)<=_JSV_NAME_END)) {
    JsVarRef n = jsvGetFirstChild(pa);
    jsvUnLock(pa);
    if (!n) {
      if (pa==a) jsvCheckReferenceError(a);
      return 0;
    }
    pa = jsvLock(n);
    flags = pa->flags&JSV_VARTYPEMASK;
    do { if (!(pa!=a)) jsAssertFail("bin/espruino_embedded.c",4212,""); } while(0);
    if (!repeat) break;
  }
  if (jsvIsGetterOrSetter(pa)) {
    JsVar *getterParent = jsvIsNewChild(a)?jsvLock(jsvGetNextSibling(a)):0;
    JsVar *v = jsvExecuteGetter(getterParent?getterParent:parent, pa);
    jsvUnLock2(getterParent,pa);
    pa = v;
  }
  return pa;
}
JsVar *jsvSkipName(JsVar *a) {
  return jsvSkipNameWithParent(a, true, 0);
}
JsVar *jsvSkipOneName(JsVar *a) {
  return jsvSkipNameWithParent(a, false, 0);
}
JsVar *jsvSkipToLastName(JsVar *a) {
  do { if (!(jsvIsName(a))) jsAssertFail("bin/espruino_embedded.c",4245,""); } while(0);
  a = jsvLockAgain(a);
  while (true) {
    if (!jsvGetFirstChild(a)) return a;
    JsVar *child = jsvLock(jsvGetFirstChild(a));
    if (jsvIsName(child)) {
      jsvUnLock(a);
      a = child;
    } else {
      jsvUnLock(child);
      return a;
    }
  }
  return 0;
}
JsVar *jsvSkipNameAndUnLock(JsVar *a) {
  JsVar *b = jsvSkipName(a);
  jsvUnLock(a);
  return b;
}
JsVar *jsvSkipOneNameAndUnLock(JsVar *a) {
  JsVar *b = jsvSkipOneName(a);
  jsvUnLock(a);
  return b;
}
bool jsvIsStringEqualOrStartsWithOffset(JsVar *var, const char *str, bool isStartsWith, size_t startIdx, bool ignoreCase) {
  if (!jsvHasCharacterData(var)) {
    return 0;
  }
  JsvStringIterator it;
  jsvStringIteratorNew(&it, var, startIdx);
  if (ignoreCase) {
      while (jsvStringIteratorHasChar(&it) && *str &&
          charToLowerCase(jsvStringIteratorGetChar(&it)) == charToLowerCase(*str)) {
        str++;
        jsvStringIteratorNext(&it);
      }
  } else {
      while (jsvStringIteratorHasChar(&it) && *str &&
             jsvStringIteratorGetChar(&it) == *str) {
        str++;
        jsvStringIteratorNext(&it);
      }
  }
  bool eq = (isStartsWith && !*str) ||
            jsvStringIteratorGetChar(&it)==*str;
  jsvStringIteratorFree(&it);
  return eq;
}
bool jsvIsStringEqualOrStartsWith(JsVar *var, const char *str, bool isStartsWith) {
  return jsvIsStringEqualOrStartsWithOffset(var, str, isStartsWith, 0, false);
}
bool jsvIsStringEqual(JsVar *var, const char *str) {
  return jsvIsStringEqualOrStartsWith(var, str, false);
}
bool jsvIsStringIEqualAndUnLock(JsVar *var, const char *str) {
  bool b = jsvIsStringEqualOrStartsWithOffset(var, str, false, 0, true);
  jsvUnLock(var);
  return b;
}
int jsvCompareString(JsVar *va, JsVar *vb, size_t starta, size_t startb, bool equalAtEndOfString) {
  JsvStringIterator ita, itb;
  jsvStringIteratorNewUTF8(&ita, va, starta);
  jsvStringIteratorNewUTF8(&itb, vb, startb);
  while (true) {
    int ca = jsvStringIteratorGetUTF8CharAndNext(&ita);
    int cb = jsvStringIteratorGetUTF8CharAndNext(&itb);
    if (ca != cb) {
      jsvStringIteratorFree(&ita);
      jsvStringIteratorFree(&itb);
      if ((ca<0 || cb<0) && equalAtEndOfString) return 0;
      return ca - cb;
    }
    if (ca < 0) {
      jsvStringIteratorFree(&ita);
      jsvStringIteratorFree(&itb);
      return 0;
    }
  }
  return true;
}
JsVar *jsvGetCommonCharacters(JsVar *va, JsVar *vb) {
  JsVar *v = jsvNewFromEmptyString();
  if (!v) return 0;
  JsvStringIterator ita, itb;
  jsvStringIteratorNewUTF8(&ita, va, 0);
  jsvStringIteratorNewUTF8(&itb, vb, 0);
  int ca = jsvStringIteratorGetUTF8CharAndNext(&ita);
  int cb = jsvStringIteratorGetUTF8CharAndNext(&itb);
  while (ca>0 && cb>0 && ca == cb) {
    jsvAppendCharacter(v, (char)ca);
    ca = jsvStringIteratorGetUTF8CharAndNext(&ita);
    cb = jsvStringIteratorGetUTF8CharAndNext(&itb);
  }
  jsvStringIteratorFree(&ita);
  jsvStringIteratorFree(&itb);
  return v;
}
int jsvCompareInteger(JsVar *va, JsVar *vb) {
  if (jsvIsInt(va) && jsvIsInt(vb))
    return (int)(jsvGetInteger(va) - jsvGetInteger(vb));
  else if (jsvIsInt(va))
    return -1;
  else if (jsvIsInt(vb))
    return 1;
  else
    return 0;
}
JsVar *jsvCopyNameOnly(JsVar *src, bool linkChildren, bool keepAsName) {
  do { if (!(jsvIsName(src))) jsAssertFail("bin/espruino_embedded.c",4386,""); } while(0);
  JsVarFlags flags = src->flags;
  JsVar *dst = 0;
  if (!keepAsName) {
    JsVarFlags t = src->flags & JSV_VARTYPEMASK;
    if (t>=_JSV_NAME_INT_START && t<=_JSV_NAME_INT_END) {
      flags = (flags & ~JSV_VARTYPEMASK) | JSV_INTEGER;
    } else {
      do { if (!((JSV_NAME_STRING_INT_0 < JSV_NAME_STRING_0) && (JSV_NAME_STRING_0 < JSV_STRING_0) && (JSV_STRING_0 < JSV_STRING_EXT_0))) jsAssertFail("bin/espruino_embedded.c",4396,""); } while(0);
      do { if (!(t>=JSV_NAME_STRING_INT_0 && t<=JSV_NAME_STRING_MAX)) jsAssertFail("bin/espruino_embedded.c",4397,""); } while(0);
      if (jsvGetLastChild(src)) {
        dst = jsvNewFromStringVarComplete(src);
        if (!dst) return 0;
      } else {
        flags = (flags & (JsVarFlags)~JSV_VARTYPEMASK) | (JSV_STRING_0 + jsvGetCharactersInVar(src));
      }
    }
  }
  if (!dst) {
    dst = jsvNewWithFlags(flags & JSV_VARIABLEINFOMASK);
    if (!dst) return 0;
    memcpy(&dst->varData, &src->varData, 4);
    do { if (!(jsvGetLastChild(dst) == 0)) jsAssertFail("bin/espruino_embedded.c",4415,""); } while(0);
    do { if (!(jsvGetFirstChild(dst) == 0)) jsAssertFail("bin/espruino_embedded.c",4416,""); } while(0);
    do { if (!(jsvGetPrevSibling(dst) == 0)) jsAssertFail("bin/espruino_embedded.c",4417,""); } while(0);
    do { if (!(jsvGetNextSibling(dst) == 0)) jsAssertFail("bin/espruino_embedded.c",4418,""); } while(0);
    if (jsvHasStringExt(src)) {
      do { if (!(keepAsName || !jsvGetLastChild(src))) jsAssertFail("bin/espruino_embedded.c",4422,""); } while(0);
      if (jsvGetLastChild(src)) {
        JsVar *child = jsvLock(jsvGetLastChild(src));
        JsVar *childCopy = jsvCopy(child, true);
        if (childCopy) {
          jsvSetLastChild(dst, jsvGetRef(childCopy));
          jsvUnLock(childCopy);
        }
        jsvUnLock(child);
      }
    } else {
      do { if (!(jsvIsBasic(src))) jsAssertFail("bin/espruino_embedded.c",4434,""); } while(0);
    }
  }
  if (linkChildren && jsvGetFirstChild(src)) {
    if (jsvIsNameWithValue(src))
      jsvSetFirstChild(dst, jsvGetFirstChild(src));
    else
      jsvSetFirstChild(dst, jsvRefRef(jsvGetFirstChild(src)));
  }
  return dst;
}
JsVar *jsvCopy(JsVar *src, bool copyChildren) {
  if (jsvIsFlatString(src)) {
    return jsvNewFromStringVarComplete(src);
  }
  JsVar *dst = jsvNewWithFlags(src->flags & JSV_VARIABLEINFOMASK);
  if (!dst) return 0;
  if (!jsvIsStringExt(src)) {
      bool refsAsData = jsvIsBasicString(src)||jsvIsNativeString(src)||jsvIsFlashString(src)||jsvIsNativeFunction(src);
      memcpy(&dst->varData, &src->varData, refsAsData ? (4 + ((14*3 + 0)>>3)) : 4);
      if (jsvIsNativeFunction(src)) {
        jsvSetFirstChild(dst,0);
      }
      if (!refsAsData) {
        do { if (!(jsvGetPrevSibling(dst) == 0)) jsAssertFail("bin/espruino_embedded.c",4461,""); } while(0);
        do { if (!(jsvGetNextSibling(dst) == 0)) jsAssertFail("bin/espruino_embedded.c",4462,""); } while(0);
        do { if (!(jsvGetFirstChild(dst) == 0)) jsAssertFail("bin/espruino_embedded.c",4463,""); } while(0);
      }
      do { if (!(jsvGetLastChild(dst) == 0)) jsAssertFail("bin/espruino_embedded.c",4465,""); } while(0);
  } else {
    memcpy(&dst->varData, &src->varData, (4 + ((14*3 + 0 + 8)>>3)));
    do { if (!(jsvGetLastChild(dst) == 0)) jsAssertFail("bin/espruino_embedded.c",4470,""); } while(0);
  }
  if (copyChildren && jsvIsName(src)) {
    if (jsvGetFirstChild(src)) {
      if (jsvIsNameWithValue(src)) {
        jsvSetFirstChild(dst, jsvGetFirstChild(src));
      } else {
        JsVar *child = jsvLock(jsvGetFirstChild(src));
        JsVar *childCopy = jsvRef(jsvCopy(child, true));
        jsvUnLock(child);
        if (childCopy) {
          jsvSetFirstChild(dst, jsvGetRef(childCopy));
          jsvUnLock(childCopy);
        }
      }
    }
  }
  if (jsvHasStringExt(src)) {
    src = jsvLockAgain(src);
    JsVar *dstChild = jsvLockAgain(dst);
    while (jsvGetLastChild(src)) {
      JsVar *child = jsvLock(jsvGetLastChild(src));
      if (jsvIsStringExt(child)) {
        JsVar *childCopy = jsvNewWithFlags(child->flags & JSV_VARIABLEINFOMASK);
        if (childCopy) {
          memcpy(&childCopy->varData, &child->varData, (4 + ((14*3 + 0 + 8)>>3)));
          jsvSetLastChild(dstChild, jsvGetRef(childCopy));
        }
        jsvUnLock2(src,dstChild);
        src = child;
        dstChild = childCopy;
      } else {
        JsVar *childCopy = jsvCopy(child, true);
        if (childCopy) {
          jsvSetLastChild(dstChild, jsvGetRef(jsvRef(childCopy)));
          jsvUnLock(childCopy);
        }
        jsvUnLock3(src, dstChild, child);
        return dst;
      }
    }
    jsvUnLock2(src,dstChild);
  } else if (jsvHasChildren(src)) {
    if (copyChildren) {
      JsVarRef vr;
      vr = jsvGetFirstChild(src);
      while (vr) {
        JsVar *name = jsvLock(vr);
        JsVar *child = jsvCopyNameOnly(name, true , true );
        if (child) {
          jsvAddName(dst, child);
          jsvUnLock(child);
        }
        vr = jsvGetNextSibling(name);
        jsvUnLock(name);
      }
    }
  } else {
    do { if (!(jsvIsBasic(src))) jsAssertFail("bin/espruino_embedded.c",4534,""); } while(0);
  }
  return dst;
}
void jsvAddName(JsVar *parent, JsVar *namedChild) {
  namedChild = jsvRef(namedChild);
  do { if (!(jsvIsName(namedChild))) jsAssertFail("bin/espruino_embedded.c",4542,""); } while(0);
  if (jsvIsArray(parent) && jsvIsInt(namedChild)) {
    JsVarInt index = namedChild->varData.integer;
    if (index >= jsvGetArrayLength(parent)) {
      jsvSetArrayLength(parent, index + 1, false);
    }
  }
  if (jsvGetLastChild(parent)) {
    JsVar *insertAfter = jsvLock(jsvGetLastChild(parent));
    if (jsvIsArray(parent)) {
      while (insertAfter && jsvCompareInteger(namedChild, insertAfter)<0) {
        JsVarRef prev = jsvGetPrevSibling(insertAfter);
        jsvUnLock(insertAfter);
        insertAfter = jsvLockSafe(prev);
      }
    }
    if (insertAfter) {
      do { if (!(jsvIsName(insertAfter))) jsAssertFail("bin/espruino_embedded.c",4564,""); } while(0);
      if (jsvGetNextSibling(insertAfter)) {
        JsVar *insertBefore = jsvLock(jsvGetNextSibling(insertAfter));
        jsvSetPrevSibling(insertBefore, jsvGetRef(namedChild));
        jsvSetNextSibling(namedChild, jsvGetRef(insertBefore));
        jsvUnLock(insertBefore);
      } else {
        jsvSetLastChild(parent, jsvGetRef(namedChild));
      }
      jsvSetNextSibling(insertAfter, jsvGetRef(namedChild));
      jsvSetPrevSibling(namedChild, jsvGetRef(insertAfter));
      jsvUnLock(insertAfter);
    } else {
      JsVar *firstChild = jsvLock(jsvGetFirstChild(parent));
      jsvSetPrevSibling(firstChild, jsvGetRef(namedChild));
      jsvUnLock(firstChild);
      jsvSetNextSibling(namedChild, jsvGetFirstChild(parent));
      jsvSetFirstChild(parent, jsvGetRef(namedChild));
    }
  } else {
    JsVarRef r = jsvGetRef(namedChild);
    jsvSetFirstChild(parent, r);
    jsvSetLastChild(parent, r);
  }
}
JsVar *jsvAddNamedChild(JsVar *parent, JsVar *value, const char *name) {
  do { if (!(parent)) jsAssertFail("bin/espruino_embedded.c",4596,""); } while(0);
  JsVar *namedChild = jsvNewNameFromString(name);
  if (!namedChild) return 0;
  if (value) jsvSetFirstChild(namedChild, jsvGetRef(jsvRef(value)));
  jsvAddName(parent, namedChild);
  return namedChild;
}
void jsvAddNamedChildAndUnLock(JsVar *parent, JsVar *value, const char *name) {
  jsvUnLock2(jsvAddNamedChild(parent, value, name), value);
}
JsVar *jsvSetValueOfName(JsVar *name, JsVar *src) {
  do { if (!(name && jsvIsName(name))) jsAssertFail("bin/espruino_embedded.c",4609,""); } while(0);
  do { if (!(name!=src)) jsAssertFail("bin/espruino_embedded.c",4610,""); } while(0);
  if (jsvIsNameWithValue(name)) {
    if (jsvIsString(name))
      name->flags = (name->flags & (JsVarFlags)~JSV_VARTYPEMASK) | (JSV_NAME_STRING_0 + jsvGetCharactersInVar(name));
    else
      name->flags = (name->flags & (JsVarFlags)~JSV_VARTYPEMASK) | JSV_NAME_INT;
    jsvSetFirstChild(name, 0);
  } else if (jsvGetFirstChild(name))
    jsvUnRefRef(jsvGetFirstChild(name));
  if (src) {
    if (jsvIsInt(name)) {
      if ((jsvIsInt(src) || jsvIsBoolean(src)) && !jsvIsPin(src)) {
        JsVarInt v = src->varData.integer;
        if (v>=(-(1<<(14 -1))) && v<=((1<<(14 -1))-1)) {
          name->flags = (name->flags & (JsVarFlags)~JSV_VARTYPEMASK) | (jsvIsInt(src) ? JSV_NAME_INT_INT : JSV_NAME_INT_BOOL);
          jsvSetFirstChild(name, (JsVarRef)v);
          return name;
        }
      }
    } else if (jsvIsString(name) && !jsvIsUTF8String(name)) {
      if (jsvIsInt(src) && !jsvIsPin(src)) {
        JsVarInt v = src->varData.integer;
        if (v>=(-(1<<(14 -1))) && v<=((1<<(14 -1))-1)) {
          name->flags = (name->flags & (JsVarFlags)~JSV_VARTYPEMASK) | (JSV_NAME_STRING_INT_0 + jsvGetCharactersInVar(name));
          jsvSetFirstChild(name, (JsVarRef)v);
          return name;
        }
      }
    }
    jsvSetFirstChild(name, jsvGetRef(jsvRef(src)));
  } else
    jsvSetFirstChild(name, 0);
  return name;
}
JsVar *jsvFindChildFromString(JsVar *parent, const char *name) {
  char fastCheck[4] = {0,0,0,0};
  bool superFastCheck = true;
  fastCheck[0] = name[0];
  if (name[0]) {
    fastCheck[1] = name[1];
    if (name[1]) {
      fastCheck[2] = name[2];
      if (name[2]) {
        fastCheck[3] = name[3];
        if (name[3])
          superFastCheck = name[4]==0;
      }
    }
  }
  do { if (!(jsvHasChildren(parent))) jsAssertFail("bin/espruino_embedded.c",4674,""); } while(0);
  JsVarRef childref = jsvGetFirstChild(parent);
  if (!superFastCheck) {
    while (childref) {
      JsVar *child = jsvGetAddressOf(childref);
      if (*(int*)fastCheck==*(int*)child->varData.str &&
          jsvIsStringEqual(child, name)) {
        return jsvLockAgain(child);
      }
      childref = jsvGetNextSibling(child);
    }
  } else {
    size_t charsInName = 0;
    while (name[charsInName])
      charsInName++;
    while (childref) {
      JsVar *child = jsvGetAddressOf(childref);
      if (*(int*)fastCheck==*(int*)child->varData.str &&
          !child->varData.ref.lastChild &&
          jsvGetCharactersInVar(child)==charsInName) {
        return jsvLockAgain(child);
      }
      childref = jsvGetNextSibling(child);
    }
  }
  return 0;
}
JsVar *jsvFindOrAddChildFromString(JsVar *parent, const char *name) {
  JsVar *child = jsvFindChildFromString(parent, name);
  if (!child) {
    child = jsvNewNameFromString(name);
    if (child)
      jsvAddName(parent, child);
  }
  return child;
}
JsVar *jsvFindChildFromStringI(JsVar *parent, const char *name) {
  do { if (!(jsvHasChildren(parent))) jsAssertFail("bin/espruino_embedded.c",4717,""); } while(0);
  JsVarRef childref = jsvGetFirstChild(parent);
  while (childref) {
    JsVar *child = jsvGetAddressOf(childref);
    if (jsvHasCharacterData(child) &&
        jsvIsStringEqualOrStartsWithOffset(child, name, false, 0, true)) {
      return jsvLockAgain(child);
    }
    childref = jsvGetNextSibling(child);
  }
  return 0;
}
JsVar *jsvCreateNewChild(JsVar *parent, JsVar *index, JsVar *child) {
  JsVar *newChild = jsvAsName(index);
  if (!newChild) return 0;
  do { if (!(!jsvGetFirstChild(newChild))) jsAssertFail("bin/espruino_embedded.c",4737,""); } while(0);
  if (child) jsvSetValueOfName(newChild, child);
  do { if (!(!jsvGetNextSibling(newChild) && !jsvGetPrevSibling(newChild))) jsAssertFail("bin/espruino_embedded.c",4739,""); } while(0);
  JsVarRef r = jsvGetRef(jsvRef(jsvRef(parent)));
  jsvSetNextSibling(newChild, r);
  jsvSetPrevSibling(newChild, r);
  return newChild;
}
JsVar *jsvAsName(JsVar *var) {
  if (!var) return 0;
  if (jsvGetRefs(var) == 0) {
    if (!jsvIsName(var))
      return jsvMakeIntoVariableName(jsvLockAgain(var), 0);
    return jsvLockAgain(var);
  } else {
    return jsvMakeIntoVariableName(jsvCopy(var, false), 0);
  }
}
JsVar *jsvFindChildFromVar(JsVar *parent, JsVar *childName, bool addIfNotFound) {
  JsVar *child;
  JsVarRef childref = jsvGetFirstChild(parent);
  while (childref) {
    child = jsvLock(childref);
    if (jsvIsBasicVarEqual(child, childName)) {
      return child;
    }
    childref = jsvGetNextSibling(child);
    jsvUnLock(child);
  }
  child = 0;
  if (addIfNotFound && childName) {
    child = jsvAsName(childName);
    jsvAddName(parent, child);
  }
  return child;
}
void jsvRemoveChild(JsVar *parent, JsVar *child) {
  do { if (!(jsvHasChildren(parent))) jsAssertFail("bin/espruino_embedded.c",4789,""); } while(0);
  do { if (!(jsvIsName(child))) jsAssertFail("bin/espruino_embedded.c",4790,""); } while(0);
  JsVarRef childref = jsvGetRef(child);
  bool wasChild = false;
  if (jsvGetFirstChild(parent) == childref) {
    jsvSetFirstChild(parent, jsvGetNextSibling(child));
    wasChild = true;
  }
  if (jsvGetLastChild(parent) == childref) {
    jsvSetLastChild(parent, jsvGetPrevSibling(child));
    wasChild = true;
    if (jsvIsArray(parent)) {
      JsVarInt l = 0;
      if (jsvGetLastChild(parent))
        l = jsvGetIntegerAndUnLock(jsvLock(jsvGetLastChild(parent)))+1;
      jsvSetArrayLength(parent, l, false);
    }
  }
  if (jsvGetPrevSibling(child)) {
    JsVar *v = jsvLock(jsvGetPrevSibling(child));
    do { if (!(jsvGetNextSibling(v) == jsvGetRef(child))) jsAssertFail("bin/espruino_embedded.c",4818,""); } while(0);
    jsvSetNextSibling(v, jsvGetNextSibling(child));
    jsvUnLock(v);
    wasChild = true;
  }
  if (jsvGetNextSibling(child)) {
    JsVar *v = jsvLock(jsvGetNextSibling(child));
    do { if (!(jsvGetPrevSibling(v) == jsvGetRef(child))) jsAssertFail("bin/espruino_embedded.c",4825,""); } while(0);
    jsvSetPrevSibling(v, jsvGetPrevSibling(child));
    jsvUnLock(v);
    wasChild = true;
  }
  jsvSetPrevSibling(child, 0);
  jsvSetNextSibling(child, 0);
  if (wasChild)
    jsvUnRef(child);
}
void jsvRemoveChildAndUnLock(JsVar *parent, JsVar *child) {
  jsvRemoveChild(parent, child);
  jsvUnLock(child);
}
void jsvRemoveAllChildren(JsVar *parent) {
  do { if (!(jsvHasChildren(parent))) jsAssertFail("bin/espruino_embedded.c",4843,""); } while(0);
  while (jsvGetFirstChild(parent)) {
    JsVar *v = jsvLock(jsvGetFirstChild(parent));
    jsvRemoveChildAndUnLock(parent, v);
  }
}
bool jsvIsChild(JsVar *parent, JsVar *child) {
  do { if (!(jsvIsArray(parent) || jsvIsObject(parent) || jsvIsFunction(parent))) jsAssertFail("bin/espruino_embedded.c",4852,""); } while(0);
  do { if (!(jsvIsName(child))) jsAssertFail("bin/espruino_embedded.c",4853,""); } while(0);
  JsVarRef childref = jsvGetRef(child);
  JsVarRef indexref;
  indexref = jsvGetFirstChild(parent);
  while (indexref) {
    if (indexref == childref) return true;
    JsVar *indexVar = jsvLock(indexref);
    indexref = jsvGetNextSibling(indexVar);
    jsvUnLock(indexVar);
  }
  return false;
}
JsVar *jsvObjectGetChild(JsVar *obj, const char *name, JsVarFlags createChild) {
  if (!obj) return 0;
  do { if (!(jsvHasChildren(obj))) jsAssertFail("bin/espruino_embedded.c",4870,""); } while(0);
  JsVar *childName = createChild ? jsvFindOrAddChildFromString(obj, name) :
                                   jsvFindChildFromString(obj, name);
  JsVar *child = jsvSkipName(childName);
  if (!child && createChild && childName!=0 ) {
    child = jsvNewWithFlags(createChild);
    jsvSetValueOfName(childName, child);
    jsvUnLock(childName);
    return child;
  }
  jsvUnLock(childName);
  return child;
}
JsVar *jsvObjectGetChildIfExists(JsVar *obj, const char *name) {
  if (!obj) return 0;
  do { if (!(jsvHasChildren(obj))) jsAssertFail("bin/espruino_embedded.c",4887,""); } while(0);
  return jsvSkipNameAndUnLock(jsvFindChildFromString(obj, name));
}
JsVar *jsvObjectGetChildI(JsVar *obj, const char *name) {
  if (!obj) return 0;
  do { if (!(jsvHasChildren(obj))) jsAssertFail("bin/espruino_embedded.c",4894,""); } while(0);
  return jsvSkipNameAndUnLock(jsvFindChildFromStringI(obj, name));
}
bool jsvObjectGetBoolChild(JsVar *obj, const char *name) {
  return jsvObjectGetIntegerChild(obj,name)!=0;
}
JsVarInt jsvObjectGetIntegerChild(JsVar *obj, const char *name) {
  if (!obj) return 0;
  do { if (!(jsvHasChildren(obj))) jsAssertFail("bin/espruino_embedded.c",4908,""); } while(0);
  JsVar *v = jsvFindChildFromString(obj, name);
  if (jsvIsNameInt(v) || jsvIsNameIntBool(v)) {
    JsVarInt vi = jsvGetFirstChildSigned(v);
    jsvUnLock(v);
    return vi;
  }
  return jsvGetIntegerAndUnLock(jsvSkipNameAndUnLock(v));
}
JsVarFloat jsvObjectGetFloatChild(JsVar *obj, const char *name) {
  return jsvGetFloatAndUnLock(jsvObjectGetChildIfExists(obj, name));
}
JsVar *jsvObjectSetChild(JsVar *obj, const char *name, JsVar *child) {
  do { if (!(jsvHasChildren(obj))) jsAssertFail("bin/espruino_embedded.c",4927,""); } while(0);
  if (!jsvHasChildren(obj)) return 0;
  JsVar *childName = jsvFindOrAddChildFromString(obj, name);
  if (!childName) return 0;
  jsvSetValueOfName(childName, child);
  jsvUnLock(childName);
  return child;
}
JsVar *jsvObjectSetChildVar(JsVar *obj, JsVar *name, JsVar *child) {
  do { if (!(jsvHasChildren(obj))) jsAssertFail("bin/espruino_embedded.c",4939,""); } while(0);
  if (!jsvHasChildren(obj)) return 0;
  JsVar *childName = jsvFindChildFromVar(obj, name, true);
  if (!childName) return 0;
  jsvSetValueOfName(childName, child);
  jsvUnLock(childName);
  return child;
}
void jsvObjectSetChildAndUnLock(JsVar *obj, const char *name, JsVar *child) {
  jsvObjectSetChild(obj, name, child);
  jsvUnLock(child);
}
void jsvObjectRemoveChild(JsVar *obj, const char *name) {
  JsVar *child = jsvFindChildFromString(obj, name);
  if (child)
    jsvRemoveChildAndUnLock(obj, child);
}
JsVar *jsvObjectSetOrRemoveChild(JsVar *obj, const char *name, JsVar *child) {
  if (child)
    jsvObjectSetChild(obj, name, child);
  else
    jsvObjectRemoveChild(obj, name);
  return child;
}
void jsvObjectAppendAll(JsVar *target, JsVar *source) {
  do { if (!(jsvHasChildren(target))) jsAssertFail("bin/espruino_embedded.c",4972,""); } while(0);
  do { if (!(jsvHasChildren(source))) jsAssertFail("bin/espruino_embedded.c",4973,""); } while(0);
  JsvObjectIterator it;
  jsvObjectIteratorNew(&it, source);
  while (jsvObjectIteratorHasValue(&it)) {
    JsVar *k = jsvObjectIteratorGetKey(&it);
    JsVar *v = jsvSkipName(k);
    if (!jsvIsInternalObjectKey(k))
      jsvObjectSetChildVar(target, k, v);
    jsvUnLock2(k,v);
    jsvObjectIteratorNext(&it);
  }
  jsvObjectIteratorFree(&it);
}
int jsvGetChildren(const JsVar *v) {
  int children = 0;
  JsVarRef childref = jsvGetFirstChild(v);
  while (childref) {
    JsVar *child = jsvLock(childref);
    children++;
    childref = jsvGetNextSibling(child);
    jsvUnLock(child);
  }
  return children;
}
JsVar *jsvGetFirstName(JsVar *v) {
  do { if (!(jsvHasChildren(v))) jsAssertFail("bin/espruino_embedded.c",5002,""); } while(0);
  if (!jsvGetFirstChild(v)) return 0;
  return jsvLock(jsvGetFirstChild(v));
}
JsVarInt jsvGetArrayLength(const JsVar *arr) {
  if (!arr) return 0;
  do { if (!(jsvIsArray(arr))) jsAssertFail("bin/espruino_embedded.c",5009,""); } while(0);
  return arr->varData.integer;
}
JsVarInt jsvSetArrayLength(JsVar *arr, JsVarInt length, bool truncate) {
  do { if (!(jsvIsArray(arr))) jsAssertFail("bin/espruino_embedded.c",5014,""); } while(0);
  if (truncate && length < arr->varData.integer) {
  }
  arr->varData.integer = length;
  return length;
}
JsVarInt jsvGetLength(const JsVar *src) {
  if (jsvIsArray(src)) {
    return jsvGetArrayLength(src);
  } else if (jsvIsArrayBuffer(src)) {
    return (JsVarInt)jsvGetArrayBufferLength(src);
  } else if (jsvIsString(src)) {
    return (JsVarInt)jsvGetStringLength(src);
  } else if (jsvIsObject(src) || jsvIsFunction(src)) {
    return jsvGetChildren(src);
  } else {
    return 1;
  }
}
static size_t _jsvCountJsVarsUsedRecursive(JsVar *v, bool resetRecursionFlag) {
  if (!v) return 0;
  if (resetRecursionFlag) {
    if (!(v->flags & JSV_IS_RECURSING))
      return 0;
    v->flags &= ~JSV_IS_RECURSING;
  } else {
    if (v->flags & JSV_IS_RECURSING)
      return 0;
    v->flags |= JSV_IS_RECURSING;
  }
  size_t count = 1;
  if (jsvHasSingleChild(v) || jsvHasChildren(v)) {
    JsVarRef childref = jsvGetFirstChild(v);
    while (childref) {
      JsVar *child = jsvLock(childref);
      count += _jsvCountJsVarsUsedRecursive(child, resetRecursionFlag);
      if (jsvHasChildren(v)) childref = jsvGetNextSibling(child);
      else childref = 0;
      jsvUnLock(child);
    }
  } else if (jsvIsFlatString(v))
    count += jsvGetFlatStringBlocks(v);
  if (jsvHasCharacterData(v)) {
    JsVarRef childref = jsvGetLastChild(v);
    while (childref) {
      JsVar *child = jsvLock(childref);
      count++;
      childref = jsvGetLastChild(child);
      jsvUnLock(child);
    }
  }
  if (jsvIsName(v) && !jsvIsNameWithValue(v) && jsvGetFirstChild(v)) {
    JsVar *child = jsvLock(jsvGetFirstChild(v));
    count += _jsvCountJsVarsUsedRecursive(child, resetRecursionFlag);
    jsvUnLock(child);
  }
  return count;
}
size_t jsvCountJsVarsUsed(JsVar *v) {
  if ((execInfo.root) && (v != execInfo.root)) execInfo.root->flags |= JSV_IS_RECURSING;
  size_t c = _jsvCountJsVarsUsedRecursive(v, false);
  _jsvCountJsVarsUsedRecursive(v, true);
  if ((execInfo.root) && (v != execInfo.root)) execInfo.root->flags &= ~JSV_IS_RECURSING;
  return c;
}
JsVar *jsvGetArrayIndex(const JsVar *arr, JsVarInt index) {
  JsVarRef childref = jsvGetLastChild(arr);
  JsVarInt lastArrayIndex = 0;
  while (childref) {
    JsVar *child = jsvLock(childref);
    if (jsvIsInt(child)) {
      lastArrayIndex = child->varData.integer;
      if (lastArrayIndex == index) {
        return child;
      }
      jsvUnLock(child);
      break;
    }
    childref = jsvGetPrevSibling(child);
    jsvUnLock(child);
  }
  if (index > lastArrayIndex)
    return 0;
  if (index > lastArrayIndex/2) {
    while (childref) {
      JsVar *child = jsvLock(childref);
      do { if (!(jsvIsInt(child))) jsAssertFail("bin/espruino_embedded.c",5119,""); } while(0);
      if (child->varData.integer == index) {
        return child;
      }
      childref = jsvGetPrevSibling(child);
      jsvUnLock(child);
    }
  } else {
    childref = jsvGetFirstChild(arr);
    while (childref) {
      JsVar *child = jsvLock(childref);
      do { if (!(jsvIsInt(child))) jsAssertFail("bin/espruino_embedded.c",5132,""); } while(0);
      if (child->varData.integer == index) {
        return child;
      }
      childref = jsvGetNextSibling(child);
      jsvUnLock(child);
    }
  }
  return 0;
}
JsVar *jsvGetArrayItem(const JsVar *arr, JsVarInt index) {
  return jsvSkipNameAndUnLock(jsvGetArrayIndex(arr,index));
}
JsVar *jsvGetLastArrayItem(const JsVar *arr) {
  JsVarRef childref = jsvGetLastChild(arr);
  if (!childref) return 0;
  return jsvSkipNameAndUnLock(jsvLock(childref));
}
void jsvSetArrayItem(JsVar *arr, JsVarInt index, JsVar *item) {
  JsVar *indexVar = jsvGetArrayIndex(arr, index);
  if (indexVar) {
    jsvSetValueOfName(indexVar, item);
  } else {
    indexVar = jsvMakeIntoVariableName(jsvNewFromInteger(index), item);
    if (indexVar)
      jsvAddName(arr, indexVar);
  }
  jsvUnLock(indexVar);
}
void jsvGetArrayItems(JsVar *arr, unsigned int itemCount, JsVar **itemPtr) {
  JsvObjectIterator it;
  jsvObjectIteratorNew(&it, arr);
  unsigned int i = 0;
  while (jsvObjectIteratorHasValue(&it)) {
    if (i<itemCount)
      itemPtr[i++] = jsvObjectIteratorGetValue(&it);
    jsvObjectIteratorNext(&it);
  }
  jsvObjectIteratorFree(&it);
  while (i<itemCount)
    itemPtr[i++] = 0;
}
JsVar *jsvGetIndexOfFull(JsVar *arr, JsVar *value, bool matchExact, bool matchIntegerIndices, int startIdx) {
  if (!jsvIsIterable(arr)) return 0;
  JsvIterator it;
  jsvIteratorNew(&it, arr, JSIF_DEFINED_ARRAY_ElEMENTS);
  while (jsvIteratorHasElement(&it)) {
    JsVar *childIndex = jsvIteratorGetKey(&it);
    if (!matchIntegerIndices ||
        (jsvIsInt(childIndex) && jsvGetInteger(childIndex)>=startIdx)) {
      JsVar *childValue = jsvIteratorGetValue(&it);
      if (childValue==value ||
          (!matchExact && jsvMathsOpTypeEqual(childValue, value))) {
        jsvUnLock(childValue);
        jsvIteratorFree(&it);
        return childIndex;
      }
      jsvUnLock(childValue);
    }
    jsvUnLock(childIndex);
    jsvIteratorNext(&it);
  }
  jsvIteratorFree(&it);
  return 0;
}
JsVar *jsvGetIndexOf(JsVar *arr, JsVar *value, bool matchExact) {
  return jsvGetIndexOfFull(arr, value, matchExact, false, 0);
}
JsVarInt jsvArrayAddToEnd(JsVar *arr, JsVar *value, JsVarInt initialValue) {
  do { if (!(jsvIsArray(arr))) jsAssertFail("bin/espruino_embedded.c",5214,""); } while(0);
  JsVarInt index = initialValue;
  if (jsvGetLastChild(arr)) {
    JsVar *last = jsvLock(jsvGetLastChild(arr));
    index = jsvGetInteger(last)+1;
    jsvUnLock(last);
  }
  JsVar *idx = jsvMakeIntoVariableName(jsvNewFromInteger(index), value);
  if (!idx) return 0;
  jsvAddName(arr, idx);
  jsvUnLock(idx);
  return index+1;
}
JsVarInt jsvArrayPush(JsVar *arr, JsVar *value) {
  do { if (!(jsvIsArray(arr))) jsAssertFail("bin/espruino_embedded.c",5231,""); } while(0);
  JsVarInt index = jsvGetArrayLength(arr);
  JsVar *idx = jsvMakeIntoVariableName(jsvNewFromInteger(index), value);
  if (!idx) return 0;
  jsvAddName(arr, idx);
  jsvUnLock(idx);
  return jsvGetArrayLength(arr);
}
JsVarInt jsvArrayPushAndUnLock(JsVar *arr, JsVar *value) {
  JsVarInt l = jsvArrayPush(arr, value);
  jsvUnLock(value);
  return l;
}
JsVarInt jsvArrayPushString(JsVar *arr, const char *string) {
  return jsvArrayPushAndUnLock(arr, jsvNewFromString(string));
}
void jsvArrayPush2Int(JsVar *arr, JsVarInt a, JsVarInt b) {
  jsvArrayPushAndUnLock(arr, jsvNewFromInteger(a));
  jsvArrayPushAndUnLock(arr, jsvNewFromInteger(b));
}
void jsvArrayPushAll(JsVar *target, JsVar *source, bool checkDuplicates) {
  do { if (!(jsvIsArray(target))) jsAssertFail("bin/espruino_embedded.c",5260,""); } while(0);
  do { if (!(jsvIsArray(source))) jsAssertFail("bin/espruino_embedded.c",5261,""); } while(0);
  JsvObjectIterator it;
  jsvObjectIteratorNew(&it, source);
  while (jsvObjectIteratorHasValue(&it)) {
    JsVar *v = jsvObjectIteratorGetValue(&it);
    bool add = true;
    if (checkDuplicates) {
      JsVar *idx = jsvGetIndexOf(target, v, false);
      if (idx) {
        add = false;
        jsvUnLock(idx);
      }
    }
    if (add) jsvArrayPush(target, v);
    jsvUnLock(v);
    jsvObjectIteratorNext(&it);
  }
  jsvObjectIteratorFree(&it);
}
JsVar *jsvArrayPop(JsVar *arr) {
  do { if (!(jsvIsArray(arr))) jsAssertFail("bin/espruino_embedded.c",5283,""); } while(0);
  JsVar *child = 0;
  JsVarInt length = jsvGetArrayLength(arr);
  if (length > 0) {
    length--;
    if (jsvGetLastChild(arr)) {
      JsVarRef ref = jsvGetLastChild(arr);
      child = jsvLock(ref);
      while (child && !jsvIsInt(child)) {
        ref = jsvGetPrevSibling(child);
        jsvUnLock(child);
        if (ref) {
          child = jsvLock(ref);
        } else {
          child = 0;
        }
      }
      if (child) {
        if (jsvGetInteger(child) == length) {
          jsvRemoveChild(arr, child);
        } else {
          jsvUnLock(child);
          child = 0;
        }
      }
    }
    jsvSetArrayLength(arr, length, false);
  }
  return child;
}
JsVar *jsvArrayPopFirst(JsVar *arr) {
  do { if (!(jsvIsArray(arr))) jsAssertFail("bin/espruino_embedded.c",5323,""); } while(0);
  if (jsvGetFirstChild(arr)) {
    JsVar *child = jsvLock(jsvGetFirstChild(arr));
    if (jsvGetFirstChild(arr) == jsvGetLastChild(arr))
      jsvSetLastChild(arr, 0);
    jsvSetFirstChild(arr, jsvGetNextSibling(child));
    jsvUnRef(child);
    if (jsvGetNextSibling(child)) {
      JsVar *v = jsvLock(jsvGetNextSibling(child));
      jsvSetPrevSibling(v, 0);
      jsvUnLock(v);
    }
    jsvSetNextSibling(child, 0);
    return child;
  } else {
    return 0;
  }
}
void jsvArrayAddUnique(JsVar *arr, JsVar *v) {
  JsVar *idx = jsvGetIndexOf(arr, v, false);
  if (!idx) {
    jsvArrayPush(arr, v);
  } else {
    jsvUnLock(idx);
  }
}
JsVar *jsvArrayJoin(JsVar *arr, JsVar *filler, bool ignoreNull) {
  JsVar *str = jsvNewFromEmptyString();
  if (!str) return 0;
  do { if (!(!filler || jsvIsString(filler))) jsAssertFail("bin/espruino_embedded.c",5357,""); } while(0);
  JsvIterator it;
  jsvIteratorNew(&it, arr, JSIF_EVERY_ARRAY_ELEMENT);
  JsvStringIterator itdst;
  jsvStringIteratorNew(&itdst, str, 0);
  bool first = true;
  while (!jspIsInterrupted() && jsvIteratorHasElement(&it)) {
    JsVar *key = jsvIteratorGetKey(&it);
    if (jsvIsInt(key)) {
      if (filler && !first)
        jsvStringIteratorAppendString(&itdst, filler, 0, (0x7FFFFFFF));
      first = false;
      JsVar *value = jsvIteratorGetValue(&it);
      if (value && (!ignoreNull || !jsvIsNull(value))) {
        JsVar *valueStr = jsvAsString(value);
        if (valueStr) {
          jsvStringIteratorAppendString(&itdst, valueStr, 0, (0x7FFFFFFF));
          jsvUnLock(valueStr);
        }
      }
      jsvUnLock(value);
    }
    jsvUnLock(key);
    jsvIteratorNext(&it);
  }
  jsvIteratorFree(&it);
  jsvStringIteratorFree(&itdst);
  return str;
}
void jsvArrayInsertBefore(JsVar *arr, JsVar *beforeIndex, JsVar *element) {
  if (beforeIndex) {
    JsVar *idxVar = jsvMakeIntoVariableName(jsvNewFromInteger(0), element);
    if (!idxVar) return;
    JsVarRef idxRef = jsvGetRef(jsvRef(idxVar));
    JsVarRef prev = jsvGetPrevSibling(beforeIndex);
    if (prev) {
      JsVar *prevVar = jsvRef(jsvLock(prev));
      jsvSetInteger(idxVar, jsvGetInteger(prevVar)+1);
      jsvSetNextSibling(prevVar, idxRef);
      jsvUnLock(prevVar);
      jsvSetPrevSibling(idxVar, prev);
    } else {
      jsvSetPrevSibling(idxVar, 0);
      jsvSetFirstChild(arr, idxRef);
    }
    jsvSetPrevSibling(beforeIndex, idxRef);
    jsvSetNextSibling(idxVar, jsvGetRef(jsvRef(beforeIndex)));
    jsvUnLock(idxVar);
  } else
    jsvArrayPush(arr, element);
}
JsVar *jsvMathsOpSkipNames(JsVar *a, JsVar *b, int op) {
  JsVar *pa = jsvSkipName(a);
  JsVar *pb = jsvSkipName(b);
  JsVar *oa = jsvGetValueOf(pa);
  JsVar *ob = jsvGetValueOf(pb);
  jsvUnLock2(pa, pb);
  JsVar *res = jsvMathsOp(oa,ob,op);
  jsvUnLock2(oa, ob);
  return res;
}
JsVar *jsvMathsOpError(int op, const char *datatype) {
  char opName[32];
  jslTokenAsString(op, opName, sizeof(opName));
  jsError("Operation %s not supported on the %s datatype", opName, datatype);
  return 0;
}
bool jsvMathsOpTypeEqual(JsVar *a, JsVar *b) {
  bool eql = (a==0) == (b==0);
  if (a && b) {
    eql = ((jsvIsInt(a)||jsvIsFloat(a)) && (jsvIsInt(b)||jsvIsFloat(b))) ||
          (jsvIsString(a) && jsvIsString(b)) ||
          ((a->flags & JSV_VARTYPEMASK) == (b->flags & JSV_VARTYPEMASK));
  }
  if (eql) {
    JsVar *contents = jsvMathsOp(a,b, LEX_EQUAL);
    if (!jsvGetBool(contents)) eql = false;
    jsvUnLock(contents);
  } else {
    do { if (!(!(jsvIsString(a) && jsvIsString(b) && jsvIsBasicVarEqual(a,b)))) jsAssertFail("bin/espruino_embedded.c",5464,""); } while(0);
  }
  return eql;
}
JsVar *jsvMathsOp(JsVar *a, JsVar *b, int op) {
  if (op == LEX_TYPEEQUAL || op == LEX_NTYPEEQUAL) {
    bool eql = jsvMathsOpTypeEqual(a,b);
    if (op == LEX_TYPEEQUAL)
      return jsvNewFromBool(eql);
    else
      return jsvNewFromBool(!eql);
  }
  bool needsInt = op=='&' || op=='|' || op=='^' || op==LEX_LSHIFT || op==LEX_RSHIFT || op==LEX_RSHIFTUNSIGNED;
  bool needsNumeric = needsInt || op=='*' || op=='/' || op=='%' || op=='-';
  bool isCompare = op==LEX_EQUAL || op==LEX_NEQUAL || op=='<' || op==LEX_LEQUAL || op=='>'|| op==LEX_GEQUAL;
  if (isCompare) {
    if (jsvIsNumeric(a) && jsvIsString(b)) {
      needsNumeric = true;
      needsInt = jsvIsIntegerish(a) && jsvIsStringNumericInt(b, false);
    } else if (jsvIsNumeric(b) && jsvIsString(a)) {
      needsNumeric = true;
      needsInt = jsvIsIntegerish(b) && jsvIsStringNumericInt(a, false);
    }
  }
  if (jsvIsUndefined(a) && jsvIsUndefined(b)) {
    if (op == LEX_EQUAL)
      return jsvNewFromBool(true);
    else if (op == LEX_NEQUAL)
      return jsvNewFromBool(false);
    else
      return 0;
  } else if (needsNumeric ||
      ((jsvIsNumeric(a) || jsvIsUndefined(a) || jsvIsNull(a)) &&
          (jsvIsNumeric(b) || jsvIsUndefined(b) || jsvIsNull(b)))) {
    if (needsInt || (jsvIsIntegerish(a) && jsvIsIntegerish(b))) {
      JsVarInt da = jsvGetInteger(a);
      JsVarInt db = jsvGetInteger(b);
      switch (op) {
      case '+': return jsvNewFromLongInteger((long long)da + (long long)db);
      case '-': return jsvNewFromLongInteger((long long)da - (long long)db);
      case '*': return jsvNewFromLongInteger((long long)da * (long long)db);
      case '/': return jsvNewFromFloat((JsVarFloat)da/(JsVarFloat)db);
      case '&': return jsvNewFromInteger(da&db);
      case '|': return jsvNewFromInteger(da|db);
      case '^': return jsvNewFromInteger(da^db);
      case '%': if (db<0) db=-db;
                return db ? jsvNewFromInteger(da%db) : jsvNewFromFloat(NAN);
      case LEX_LSHIFT: return jsvNewFromInteger(da << db);
      case LEX_RSHIFT: return jsvNewFromInteger(da >> db);
      case LEX_RSHIFTUNSIGNED: return jsvNewFromLongInteger(((JsVarIntUnsigned)da) >> db);
      case LEX_EQUAL: return jsvNewFromBool(da==db && jsvIsNull(a)==jsvIsNull(b));
      case LEX_NEQUAL: return jsvNewFromBool(da!=db || jsvIsNull(a)!=jsvIsNull(b));
      case '<': return jsvNewFromBool(da<db);
      case LEX_LEQUAL: return jsvNewFromBool(da<=db);
      case '>': return jsvNewFromBool(da>db);
      case LEX_GEQUAL: return jsvNewFromBool(da>=db);
      default: return jsvMathsOpError(op, "Integer");
      }
    } else {
      JsVarFloat da = jsvGetFloat(a);
      JsVarFloat db = jsvGetFloat(b);
      switch (op) {
      case '+': return jsvNewFromFloat(da+db);
      case '-': return jsvNewFromFloat(da-db);
      case '*': return jsvNewFromFloat(da*db);
      case '/': return jsvNewFromFloat(da/db);
      case '%': return jsvNewFromFloat(jswrap_math_mod(da, db));
      case LEX_EQUAL:
      case LEX_NEQUAL: { bool equal = da==db && jsvIsNull(a)==jsvIsNull(b);
      if ((jsvIsNull(a) && jsvIsUndefined(b)) ||
          (jsvIsNull(b) && jsvIsUndefined(a))) equal = true;
      return jsvNewFromBool((op==LEX_EQUAL) ? equal : ((bool)!equal));
      }
      case '<': return jsvNewFromBool(da<db);
      case LEX_LEQUAL: return jsvNewFromBool(da<=db);
      case '>': return jsvNewFromBool(da>db);
      case LEX_GEQUAL: return jsvNewFromBool(da>=db);
      default: return jsvMathsOpError(op, "Double");
      }
    }
  } else if ((jsvIsArray(a) || jsvIsObject(a) || jsvIsFunction(a) ||
      jsvIsArray(b) || jsvIsObject(b) || jsvIsFunction(b)) &&
      jsvIsArray(a)==jsvIsArray(b) &&
      (op == LEX_EQUAL || op==LEX_NEQUAL)) {
    bool equal = a==b;
    if (jsvIsNativeFunction(a) || jsvIsNativeFunction(b)) {
      equal = a && b &&
          a->varData.native.ptr == b->varData.native.ptr &&
          a->varData.native.argTypes == b->varData.native.argTypes &&
          jsvGetFirstChild(a) == jsvGetFirstChild(b);
    }
    switch (op) {
    case LEX_EQUAL: return jsvNewFromBool(equal);
    case LEX_NEQUAL: return jsvNewFromBool(!equal);
    default: return jsvMathsOpError(op, jsvIsArray(a)?"Array":"Object");
    }
  } else {
    JsVar *da = jsvAsString(a);
    JsVar *db = jsvAsString(b);
    if (!da || !db) {
      jsvUnLock2(da, db);
      return 0;
    }
    if (op=='+') {
      JsVar *v;
      JsVarFlags daf = da->flags & JSV_VARTYPEMASK;
      if (((daf)>=JSV_STRING_0 && (daf)<=JSV_STRING_MAX) && jsvGetLocks(da)==1 && jsvGetRefs(da)==0)
        v = jsvLockAgain(da);
      else if (((daf)==JSV_FLAT_STRING || (daf)==JSV_NATIVE_STRING || false) || false) {
        v = jsvNewFromStringVarComplete(da);
      } else
        v = jsvCopy(da, false);
      if (v)
        jsvAppendStringVarComplete(v, db);
      jsvUnLock2(da, db);
      return v;
    }
    int cmp = jsvCompareString(da,db,0,0,false);
    jsvUnLock2(da, db);
    switch (op) {
    case LEX_EQUAL: return jsvNewFromBool(cmp==0);
    case LEX_NEQUAL: return jsvNewFromBool(cmp!=0);
    case '<': return jsvNewFromBool(cmp<0);
    case LEX_LEQUAL: return jsvNewFromBool(cmp<=0);
    case '>': return jsvNewFromBool(cmp>0);
    case LEX_GEQUAL: return jsvNewFromBool(cmp>=0);
    default: return jsvMathsOpError(op, "String");
    }
  }
}
JsVar *jsvNegateAndUnLock(JsVar *v) {
  JsVar *zero = jsvNewFromInteger(0);
  JsVar *res = jsvMathsOpSkipNames(zero, v, '-');
  jsvUnLock2(zero, v);
  return res;
}
static JsVar *jsvGetPathTo_int(JsVar *root, JsVar *element, int maxDepth, JsVar *ignoreParent, int *depth) {
  if (maxDepth<=0) return 0;
  int bestDepth = maxDepth+1;
  JsVar *found = 0;
  JsvIterator it;
  jsvIteratorNew(&it, root, JSIF_DEFINED_ARRAY_ElEMENTS);
  while (jsvIteratorHasElement(&it)) {
    JsVar *el = jsvIteratorGetValue(&it);
    if (el == element && root != ignoreParent) {
      JsVar *name = jsvAsStringAndUnLock(jsvIteratorGetKey(&it));
      jsvIteratorFree(&it);
      return name;
    } else if (jsvIsObject(el) || jsvIsArray(el) || jsvIsFunction(el)) {
      int d;
      JsVar *n = jsvGetPathTo_int(el, element, maxDepth-1, ignoreParent, &d);
      if (n && d<bestDepth) {
        bestDepth = d;
        JsVar *keyName = jsvIteratorGetKey(&it);
        jsvUnLock(found);
        found = jsvVarPrintf(jsvIsObject(el) ? "%v.%v" : "%v[%q]",keyName,n);
        jsvUnLock(keyName);
      }
      jsvUnLock(n);
    }
    jsvIteratorNext(&it);
  }
  jsvIteratorFree(&it);
  *depth = bestDepth;
  return found;
}
JsVar *jsvGetPathTo(JsVar *root, JsVar *element, int maxDepth, JsVar *ignoreParent) {
  int depth = 0;
  return jsvGetPathTo_int(root, element, maxDepth, ignoreParent, &depth);
}
void jsvTraceLockInfo(JsVar *v) {
  jsiConsolePrintf("#%d[r%d,l%d] ",jsvGetRef(v),jsvGetRefs(v),jsvGetLocks(v));
}
int _jsvTraceGetLowestLevel(JsVar *var, JsVar *searchVar) {
  if (var == searchVar) return 0;
  int found = -1;
  if (var->flags & JSV_IS_RECURSING)
    return -1;
  var->flags |= JSV_IS_RECURSING;
  if (jsvHasSingleChild(var) && jsvGetFirstChild(var)) {
    JsVar *child = jsvLock(jsvGetFirstChild(var));
    int f = _jsvTraceGetLowestLevel(child, searchVar);
    jsvUnLock(child);
    if (f>=0 && (found<0 || f<found)) found=f+1;
  }
  if (jsvHasChildren(var)) {
    JsVarRef childRef = jsvGetFirstChild(var);
    while (childRef) {
      JsVar *child = jsvLock(childRef);
      int f = _jsvTraceGetLowestLevel(child, searchVar);
      if (f>=0 && (found<0 || f<found)) found=f+1;
      childRef = jsvGetNextSibling(child);
      jsvUnLock(child);
    }
  }
  var->flags &= ~JSV_IS_RECURSING;
  return found;
}
void _jsvTrace(JsVar *var, int indent, JsVar *baseVar, int level) {
  int i;
  for (i=0;i<indent;i++) jsiConsolePrintString(" ");
  if (!var) {
    jsiConsolePrintString("undefined");
    return;
  }
  if (level>0 && var==execInfo.root) {
    jsiConsolePrintString("ROOT");
    return;
  }
  jsvTraceLockInfo(var);
  int lowestLevel = _jsvTraceGetLowestLevel(baseVar, var);
  if (level>16 || (lowestLevel>=0 && lowestLevel < level)) {
    jsiConsolePrintString("...\n");
    return;
  }
  if (jsvIsNewChild(var)) {
    jsiConsolePrintString("NewChild PARENT:");
    JsVar *parent = jsvGetAddressOf(jsvGetNextSibling(var));
    _jsvTrace(parent, indent+2, baseVar, level+1);
    jsiConsolePrintString("CHILD: ");
  } else if (jsvIsName(var)) {
    jsiConsolePrintString("Name ");
  }
  char endBracket = ' ';
  if (jsvIsObject(var)) { jsiConsolePrintString("Object { "); endBracket = '}'; }
  else if (jsvIsGetterOrSetter(var)) { jsiConsolePrintString("Getter/Setter { "); endBracket = '}'; }
  else if (jsvIsArray(var)) { jsiConsolePrintf("Array(%d) [ ", var->varData.integer); endBracket = ']'; }
  else if (jsvIsNativeFunction(var)) { jsiConsolePrintf("NativeFunction 0x%x (%d) { ", var->varData.native.ptr, var->varData.native.argTypes); endBracket = '}'; }
  else if (jsvIsFunction(var)) {
    jsiConsolePrintString("Function { ");
    if (jsvIsFunctionReturn(var)) jsiConsolePrintString("return ");
    endBracket = '}';
  } else if (jsvIsPin(var)) jsiConsolePrintf("Pin %d", jsvGetInteger(var));
  else if (jsvIsInt(var)) jsiConsolePrintf("Integer %d", jsvGetInteger(var));
  else if (jsvIsBoolean(var)) jsiConsolePrintf("Bool %s", jsvGetBool(var)?"true":"false");
  else if (jsvIsFloat(var)) jsiConsolePrintf("Double %f", jsvGetFloat(var));
  else if (jsvIsFunctionParameter(var)) jsiConsolePrintf("Param %q ", var);
  else if (jsvIsArrayBufferName(var)) jsiConsolePrintf("ArrayBufferName[%d] ", jsvGetInteger(var));
  else if (jsvIsArrayBuffer(var)) jsiConsolePrintf("%s (offs %d, len %d)", jswGetBasicObjectName(var)?jswGetBasicObjectName(var):"unknown ArrayBuffer", var->varData.arraybuffer.byteOffset, var->varData.arraybuffer.length);
  else if (jsvIsString(var)) {
    size_t blocks = 1;
    if (jsvGetLastChild(var)) {
      JsVar *v = jsvGetAddressOf(jsvGetLastChild(var));
      blocks += jsvCountJsVarsUsed(v);
    }
    if (jsvIsFlatString(var)) {
      blocks += jsvGetFlatStringBlocks(var);
    }
    const char *name = "";
    if (jsvIsFlatString(var)) name="Flat";
    if (jsvIsNativeString(var)) name="Native";
    if (jsvIsFlashString(var)) name="Flash";
    jsiConsolePrintf("%sString [%d blocks] %q", name, blocks, var);
  } else {
    jsiConsolePrintf("Unknown %d", var->flags & (JsVarFlags)~(JSV_LOCK_MASK));
  }
  if (jsvIsConstant(var)) jsiConsolePrintf(" CONST ");
  if (jsvIsNameInt(var)) {
    jsiConsolePrintf(" = int %d\n", (int)jsvGetFirstChildSigned(var));
    return;
  } else if (jsvIsNameIntBool(var)) {
    jsiConsolePrintf(" = bool %s\n", jsvGetFirstChild(var)?"true":"false");
    return;
  }
  if (jsvHasSingleChild(var)) {
    JsVar *child = jsvGetFirstChild(var) ? jsvGetAddressOf(jsvGetFirstChild(var)) : 0;
    _jsvTrace(child, indent+2, baseVar, level+1);
  } else if (jsvHasChildren(var)) {
    JsvIterator it;
    jsvIteratorNew(&it, var, JSIF_DEFINED_ARRAY_ElEMENTS);
    bool first = true;
    while (jsvIteratorHasElement(&it) && !jspIsInterrupted()) {
      if (first) jsiConsolePrintf("\n");
      first = false;
      JsVar *child = jsvIteratorGetKey(&it);
      _jsvTrace(child, indent+2, baseVar, level+1);
      jsvUnLock(child);
      jsiConsolePrintf("\n");
      jsvIteratorNext(&it);
    }
    jsvIteratorFree(&it);
    if (!first)
      for (i=0;i<indent;i++) jsiConsolePrintString(" ");
  }
  jsiConsolePrintf("%c", endBracket);
}
void jsvTrace(JsVar *var, int indent) {
  MemBusyType t = isMemoryBusy;
  isMemoryBusy = 0;
  _jsvTrace(var,indent,var,0);
  isMemoryBusy = t;
  jsiConsolePrintf("\n");
}
static bool jsvGarbageCollectMarkUsed(JsVar *var) {
  var->flags &= (JsVarFlags)~JSV_GARBAGE_COLLECT;
  JsVarRef child;
  JsVar *childVar;
  if (jsvHasStringExt(var)) {
    child = jsvGetLastChild(var);
    while (child) {
      childVar = jsvGetAddressOf(child);
      childVar->flags &= (JsVarFlags)~JSV_GARBAGE_COLLECT;
      child = jsvGetLastChild(childVar);
    }
  }
  if (jsvHasSingleChild(var)) {
    if (jsvGetFirstChild(var)) {
      childVar = jsvGetAddressOf(jsvGetFirstChild(var));
      if (childVar->flags & JSV_GARBAGE_COLLECT)
        if (!jsvGarbageCollectMarkUsed(childVar)) return false;
    }
  } else if (jsvHasChildren(var)) {
    if (jsuGetFreeStack() < 256) return false;
    child = jsvGetFirstChild(var);
    while (child) {
      childVar = jsvGetAddressOf(child);
      if (childVar->flags & JSV_GARBAGE_COLLECT)
        if (!jsvGarbageCollectMarkUsed(childVar)) return false;
      child = jsvGetNextSibling(childVar);
    }
  }
  return true;
}
int jsvGarbageCollect() {
  if (isMemoryBusy) return 0;
  isMemoryBusy = MEMBUSY_GC;
  JsVarRef i;
  for (i=1;i<=jsVarsSize;i++) {
    JsVar *var = jsvGetAddressOf(i);
    if ((var->flags&JSV_VARTYPEMASK) != JSV_UNUSED) {
      var->flags |= (JsVarFlags)JSV_GARBAGE_COLLECT;
      if (jsvIsFlatString(var))
        i = (JsVarRef)(i+jsvGetFlatStringBlocks(var));
    }
  }
  for (i=1;i<=jsVarsSize;i++) {
    JsVar *var = jsvGetAddressOf(i);
    if ((var->flags & JSV_GARBAGE_COLLECT) &&
        jsvGetLocks(var)>0) {
      if (!jsvGarbageCollectMarkUsed(var)) {
        isMemoryBusy = MEM_NOT_BUSY;
        return 0;
      }
    }
    if (jsvIsFlatString(var))
      i = (JsVarRef)(i+jsvGetFlatStringBlocks(var));
  }
  unsigned int freedCount = 0;
  jsVarFirstEmpty = 0;
  JsVar *lastEmpty = 0;
  for (i=1;i<=jsVarsSize;i++) {
    JsVar *var = jsvGetAddressOf(i);
    if (var->flags & JSV_GARBAGE_COLLECT) {
      if (jsvIsFlatString(var)) {
        unsigned int count = (unsigned int)jsvGetFlatStringBlocks(var);
        freedCount+=count;
        var->flags = JSV_UNUSED;
        if (lastEmpty) jsvSetNextSibling(lastEmpty, i);
        else jsVarFirstEmpty = i;
        lastEmpty = var;
        while (count-- > 0) {
          i++;
          var = jsvGetAddressOf((JsVarRef)(i));
          var->flags = JSV_UNUSED;
          if (lastEmpty) jsvSetNextSibling(lastEmpty, i);
          else jsVarFirstEmpty = i;
          lastEmpty = var;
        }
      } else {
        if (jsvHasSingleChild(var)) {
          JsVarRef ch = jsvGetFirstChild(var);
          if (ch) {
            JsVar *child = jsvGetAddressOf(ch);
            if (child->flags!=JSV_UNUSED &&
                !(child->flags&JSV_GARBAGE_COLLECT))
              jsvUnRef(child);
          }
        }
        do { if (!(!jsvHasChildren(var) || !jsvGetFirstChild(var) || jsvGetLocks(jsvGetAddressOf(jsvGetFirstChild(var))) || jsvGetAddressOf(jsvGetFirstChild(var))->flags==JSV_UNUSED || (jsvGetAddressOf(jsvGetFirstChild(var))->flags&JSV_GARBAGE_COLLECT))) jsAssertFail("bin/espruino_embedded.c",5959,""); } while(0);
        do { if (!(!jsvHasChildren(var) || !jsvGetLastChild(var) || jsvGetLocks(jsvGetAddressOf(jsvGetLastChild(var))) || jsvGetAddressOf(jsvGetLastChild(var))->flags==JSV_UNUSED || (jsvGetAddressOf(jsvGetLastChild(var))->flags&JSV_GARBAGE_COLLECT))) jsAssertFail("bin/espruino_embedded.c",5963,""); } while(0);
        do { if (!(!jsvIsName(var) || !jsvGetPrevSibling(var) || jsvGetLocks(jsvGetAddressOf(jsvGetPrevSibling(var))) || jsvGetAddressOf(jsvGetPrevSibling(var))->flags==JSV_UNUSED || (jsvGetAddressOf(jsvGetPrevSibling(var))->flags&JSV_GARBAGE_COLLECT))) jsAssertFail("bin/espruino_embedded.c",5967,""); } while(0);
        do { if (!(!jsvIsName(var) || !jsvGetNextSibling(var) || jsvGetLocks(jsvGetAddressOf(jsvGetNextSibling(var))) || jsvGetAddressOf(jsvGetNextSibling(var))->flags==JSV_UNUSED || (jsvGetAddressOf(jsvGetNextSibling(var))->flags&JSV_GARBAGE_COLLECT))) jsAssertFail("bin/espruino_embedded.c",5971,""); } while(0);
        var->flags = JSV_UNUSED;
        if (lastEmpty) jsvSetNextSibling(lastEmpty, i);
        else jsVarFirstEmpty = i;
        lastEmpty = var;
        freedCount++;
      }
    } else if (jsvIsFlatString(var)) {
      i = (JsVarRef)(i+jsvGetFlatStringBlocks(var));
    } else if (var->flags == JSV_UNUSED) {
      if (lastEmpty) jsvSetNextSibling(lastEmpty, i);
      else jsVarFirstEmpty = i;
      lastEmpty = var;
    }
  }
  if (lastEmpty) jsvSetNextSibling(lastEmpty, 0);
  isMemoryBusy = MEM_NOT_BUSY;
  return (int)freedCount;
}
static void _jsvDefragment_moveReferences(JsVarRef defragFromRef, JsVarRef defragToRef, unsigned int lastAllocated) {
  for (JsVarRef vr=1;vr<=lastAllocated;vr++) {
    JsVar *v = _jsvGetAddressOf(vr);
    if ((v->flags&JSV_VARTYPEMASK)!=JSV_UNUSED) {
      if (jsvIsFlatString(v)) {
        vr += (unsigned int)jsvGetFlatStringBlocks(v);
      } else {
        if (jsvHasSingleChild(v) || jsvHasChildren(v))
          if (jsvGetFirstChild(v)==defragFromRef)
            jsvSetFirstChild(v,defragToRef);
        if (jsvHasStringExt(v) || jsvHasChildren(v))
          if (jsvGetLastChild(v)==defragFromRef)
            jsvSetLastChild(v,defragToRef);
        if (jsvIsName(v)) {
          if (jsvGetNextSibling(v)==defragFromRef)
            jsvSetNextSibling(v,defragToRef);
          if (jsvGetPrevSibling(v)==defragFromRef)
            jsvSetPrevSibling(v,defragToRef);
        }
      }
    }
  }
}
void jsvDefragment() {
  jsvGarbageCollect();
  jshInterruptOff();
  const unsigned int minMove = 20;
  unsigned int lastAllocated = 0;
  for (JsVarRef i=1;i<=jsVarsSize;i++) {
    JsVar *v = _jsvGetAddressOf(i);
    if ((v->flags&JSV_VARTYPEMASK)!=JSV_UNUSED) {
      if (jsvIsFlatString(v)) {
        i += 1+(unsigned int)jsvGetFlatStringBlocks(v);
      }
      lastAllocated = i;
    }
  }
  JsVarRef defragToRef = 1;
  for (JsVarRef defragFromRef=1;defragFromRef<=lastAllocated;defragFromRef++) {
    JsVar *defragTo = _jsvGetAddressOf(defragToRef);
    while ((defragTo->flags&JSV_VARTYPEMASK)!=JSV_UNUSED) {
      if (jsvIsFlatString(defragTo)) {
        defragToRef += 1+(unsigned int)jsvGetFlatStringBlocks(defragTo);
      } else defragToRef++;
      if (defragToRef > lastAllocated) {
        jsvCreateEmptyVarList();
        jshInterruptOn();
        return;
      }
      defragTo = _jsvGetAddressOf(defragToRef);
    }
    JsVar *defragFrom = _jsvGetAddressOf(defragFromRef);
    if ((defragFrom->flags&JSV_VARTYPEMASK)!=JSV_UNUSED) {
      bool canMove = jsvGetLocks(defragFrom)==0;
      if (jsvIsFlatString(defragFrom)) {
        unsigned int blocksNeeded = 1+(unsigned int)jsvGetFlatStringBlocks(defragFrom);
        if (canMove) {
          JsVarRef fsToRef = defragToRef;
          bool isClear = false;
          while (!isClear && (defragFromRef > fsToRef+minMove)) {
            isClear = true;
            for (unsigned int i=0;i<blocksNeeded;i++) {
              if ((_jsvGetAddressOf(fsToRef+i)->flags&JSV_VARTYPEMASK)!=JSV_UNUSED) {
                isClear = false;
                fsToRef += i;
                break;
              }
            }
            if (!isClear) {
              JsVar *v = _jsvGetAddressOf(fsToRef);
              while ((v->flags&JSV_VARTYPEMASK)!=JSV_UNUSED) {
                if (jsvIsFlatString(v)) {
                  fsToRef += 1+(unsigned int)jsvGetFlatStringBlocks(v);
                } else fsToRef++;
                if (fsToRef <= lastAllocated)
                  v = _jsvGetAddressOf(fsToRef);
                else
                  break;
              }
            }
          }
          if (isClear && (defragFromRef > fsToRef+minMove)) {
            defragTo = _jsvGetAddressOf(fsToRef);
            memmove(defragTo, defragFrom, sizeof(JsVar)*blocksNeeded);
            memset(defragFrom, 0, sizeof(JsVar)*blocksNeeded);
            _jsvDefragment_moveReferences(defragFromRef, fsToRef, lastAllocated);
          }
        }
        defragFromRef += blocksNeeded-1;
      } else if (canMove) {
        if (defragFromRef > defragToRef+minMove) {
          *defragTo = *defragFrom;
          memset(defragFrom, 0, sizeof(JsVar));
          _jsvDefragment_moveReferences(defragFromRef, defragToRef, lastAllocated);
        }
      }
    }
    jshKickWatchDog();
    jshKickSoftWatchDog();
  }
  jsvCreateEmptyVarList();
  jshInterruptOn();
}
void jsvDumpLockedVars() {
  jsvGarbageCollect();
  if (isMemoryBusy) return;
  isMemoryBusy = MEMBUSY_SYSTEM;
  JsVarRef i;
  for (i=1;i<=jsVarsSize;i++) {
    JsVar *var = jsvGetAddressOf(i);
    if ((var->flags&JSV_VARTYPEMASK) != JSV_UNUSED) {
      var->flags |= (JsVarFlags)JSV_GARBAGE_COLLECT;
      if (jsvIsFlatString(var))
        i = (JsVarRef)(i+jsvGetFlatStringBlocks(var));
    }
  }
  jsvGarbageCollectMarkUsed(execInfo.root);
  for (i=1;i<=jsVarsSize;i++) {
    JsVar *var = jsvGetAddressOf(i);
    if ((var->flags&JSV_VARTYPEMASK) != JSV_UNUSED) {
      if (var->flags & JSV_GARBAGE_COLLECT) {
        jsvGarbageCollectMarkUsed(var);
        jsvTrace(var, 0);
      }
      if (jsvIsFlatString(var))
        i = (JsVarRef)(i+jsvGetFlatStringBlocks(var));
    }
  }
  isMemoryBusy = MEM_NOT_BUSY;
}
void jsvDumpFreeList() {
  JsVarRef ref = jsVarFirstEmpty;
  int n = 0;
  while (ref) {
    jsiConsolePrintf("%5d ", (int)ref);
    if (++n >= 16) {
      n = 0;
      jsiConsolePrintf("\n");
    }
    JsVar *v = jsvGetAddressOf(ref);
    ref = jsvGetNextSibling(v);
  }
  jsiConsolePrintf("\n");
}
JsVar *jsvStringTrimRight(JsVar *srcString) {
  JsvStringIterator src, dst;
  JsVar *dstString = jsvNewFromEmptyString();
  jsvStringIteratorNew(&src, srcString, 0);
  jsvStringIteratorNew(&dst, dstString, 0);
  int spaces = 0;
  while (jsvStringIteratorHasChar(&src)) {
    char ch = jsvStringIteratorGetCharAndNext(&src);
    if (ch==' ') spaces++;
    else if (ch=='\n') {
      spaces = 0;
      jsvStringIteratorAppend(&dst, ch);
    } else {
      for (;spaces>0;spaces--)
        jsvStringIteratorAppend(&dst, ' ');
      jsvStringIteratorAppend(&dst, ch);
    }
  }
  jsvStringIteratorFree(&src);
  jsvStringIteratorFree(&dst);
  return dstString;
}
bool jsvIsInternalFunctionKey(JsVar *v) {
  return (jsvIsString(v) && (
      v->varData.str[0]=='\xFF')
  ) ||
  jsvIsFunctionParameter(v);
}
bool jsvIsInternalObjectKey(JsVar *v) {
  return (jsvIsString(v) && (
      v->varData.str[0]=='\xFF' ||
      jsvIsStringEqual(v, "__proto__") ||
      jsvIsStringEqual(v, "constructor")
  ));
}
JsvIsInternalChecker jsvGetInternalFunctionCheckerFor(JsVar *v) {
  if (jsvIsFunction(v)) return jsvIsInternalFunctionKey;
  if (jsvIsObject(v)) return jsvIsInternalObjectKey;
  return 0;
}
bool jsvReadConfigObject(JsVar *object, jsvConfigObject *configs, int nConfigs) {
  if (jsvIsUndefined(object)) return true;
  if (!jsvIsObject(object)) {
    jsExceptionHere(JSET_ERROR, "Expecting Object or undefined, got %t", object);
    return false;
  }
  JsvObjectIterator it;
  jsvObjectIteratorNew(&it, object);
  bool ok = true;
  while (ok && jsvObjectIteratorHasValue(&it)) {
    JsVar *key = jsvObjectIteratorGetKey(&it);
    bool found = false;
    for (int i=0;i<nConfigs;i++) {
      if (jsvIsStringEqual(key, configs[i].name)) {
        found = true;
        if (configs[i].ptr) {
          JsVar *val = jsvObjectIteratorGetValue(&it);
          switch (configs[i].type) {
          case 0: break;
          case JSV_OBJECT:
          case JSV_STRING_0:
          case JSV_ARRAY:
          case JSV_FUNCTION:
            *((JsVar**)configs[i].ptr) = jsvLockAgain(val); break;
          case JSV_BOOLEAN: *((bool*)configs[i].ptr) = jsvGetBool(val); break;
          case JSV_INTEGER: *((JsVarInt*)configs[i].ptr) = jsvGetInteger(val); break;
          case JSV_FLOAT: *((JsVarFloat*)configs[i].ptr) = jsvGetFloat(val); break;
          default: do { if (!(0)) jsAssertFail("bin/espruino_embedded.c",6260,""); } while(0); break;
          }
          jsvUnLock(val);
        }
      }
    }
    if (!found) {
      jsExceptionHere(JSET_ERROR, "Unknown option %q", key);
      ok = false;
    }
    jsvUnLock(key);
    jsvObjectIteratorNext(&it);
  }
  jsvObjectIteratorFree(&it);
  return ok;
}
JsVar *jsvCreateConfigObject(jsvConfigObject *configs, int nConfigs) {
  JsVar *o = jsvNewObject();
  if (!o) return 0;
  for (int i=0;i<nConfigs;i++) {
     if (configs[i].ptr) {
      JsVar *v = 0;
      switch (configs[i].type) {
      case 0: break;
      case JSV_OBJECT:
      case JSV_STRING_0:
      case JSV_ARRAY:
      case JSV_FUNCTION:
        v = jsvLockAgain(*((JsVar**)configs[i].ptr)); break;
      case JSV_BOOLEAN:
        v = jsvNewFromBool(*((bool*)configs[i].ptr)); break;
      case JSV_INTEGER:
        v = jsvNewFromInteger(*((JsVarInt*)configs[i].ptr)); break;
      case JSV_FLOAT:
        v = jsvNewFromFloat(*((JsVarFloat*)configs[i].ptr)); break;
      default:
        do { if (!(0)) jsAssertFail("bin/espruino_embedded.c",6303,""); } while(0);
        break;
      }
      jsvObjectSetChildAndUnLock(o, configs[i].name, v);
    }
  }
  return o;
}
bool jsvIsInstanceOf(JsVar *var, const char *constructorName) {
  bool isInst = false;
  if (!jsvHasChildren(var)) return false;
  JsVar *proto = jsvObjectGetChildIfExists(var, "__proto__");
  if (jsvIsObject(proto)) {
    JsVar *constr = jsvObjectGetChildIfExists(proto, "constructor");
    if (constr)
      isInst = jspIsConstructor(constr, constructorName);
    jsvUnLock(constr);
  }
  jsvUnLock(proto);
  return isInst;
}
JsVar *jsvNewTypedArray(JsVarDataArrayBufferViewType type, JsVarInt length) {
  JsVar *lenVar = jsvNewFromInteger(length);
  if (!lenVar) return 0;
  JsVar *array = jswrap_typedarray_constructor(type, lenVar,0,0);
  jsvUnLock(lenVar);
  return array;
}
JsVar *jsvNewDataViewWithData(JsVarInt length, unsigned char *data) {
  JsVar *buf = jswrap_arraybuffer_constructor(length);
  if (!buf) return 0;
  JsVar *view = jswrap_dataview_constructor(buf, 0, 0);
  if (!view) {
    jsvUnLock(buf);
    return 0;
  }
  if (data) {
    JsVar *arrayBufferData = jsvGetArrayBufferBackingString(buf, NULL);
    if (arrayBufferData)
      jsvSetString(arrayBufferData, (char *)data, (size_t)length);
    jsvUnLock(arrayBufferData);
  }
  jsvUnLock(buf);
  return view;
}
JsVar *jsvNewArrayBufferWithPtr(unsigned int length, char **ptr) {
  do { if (!(ptr)) jsAssertFail("bin/espruino_embedded.c",6355,""); } while(0);
  *ptr=0;
  JsVar *backingString = jsvNewFlatStringOfLength(length);
  if (!backingString) return 0;
  JsVar *arr = jsvNewArrayBufferFromString(backingString, length);
  if (!arr) {
    jsvUnLock(backingString);
    return 0;
  }
  *ptr = jsvGetFlatStringPointer(backingString);
  jsvUnLock(backingString);
  return arr;
}
JsVar *jsvNewArrayBufferWithData(JsVarInt length, unsigned char *data) {
  do { if (!(data)) jsAssertFail("bin/espruino_embedded.c",6370,""); } while(0);
  do { if (!(length>0)) jsAssertFail("bin/espruino_embedded.c",6371,""); } while(0);
  JsVar *dst = 0;
  JsVar *arr = jsvNewArrayBufferWithPtr((unsigned int)length, (char**)&dst);
  if (!dst) {
    jsvUnLock(arr);
    return 0;
  }
  memcpy(dst, data, (size_t)length);
  return arr;
}
void *jsvMalloc(size_t size) {
  do { if (!(size>0)) jsAssertFail("bin/espruino_embedded.c",6383,""); } while(0);
  JsVar *flatStr = jsvNewFlatStringOfLength((unsigned int)size);
  if (!flatStr) {
    jsErrorFlags |= JSERR_LOW_MEMORY;
    while (jsiFreeMoreMemory());
    jsvGarbageCollect();
    flatStr = jsvNewFlatStringOfLength((unsigned int)size);
  }
  void *p = (void*)jsvGetFlatStringPointer(flatStr);
  if (p) {
    memset(p,0,size);
  }
  return p;
}
void jsvFree(void *ptr) {
  JsVar *flatStr = jsvGetFlatStringFromPointer((char *)ptr);
  jsvUnLock(flatStr);
}
bool jsvIterateCallback(
    JsVar *data,
    jsvIterateCallbackFn callback,
    void *callbackData
  ) {
  bool ok = true;
  if (jsvIsNumeric(data)) {
    callback((int)jsvGetInteger(data), callbackData);
  }
  else if (jsvIsObject(data)) {
    JsVar *callbackVar = jsvObjectGetChildIfExists(data, "callback");
    if (jsvIsFunction(callbackVar)) {
      JsVar *result = jspExecuteFunction(callbackVar,0,0,NULL);
      jsvUnLock(callbackVar);
      if (result) {
        bool r = jsvIterateCallback(result, callback, callbackData);
        jsvUnLock(result);
        return r;
      }
      return true;
    }
    jsvUnLock(callbackVar);
    JsVar *countVar = jsvObjectGetChildIfExists(data, "count");
    JsVar *dataVar = jsvObjectGetChildIfExists(data, "data");
    if (countVar && dataVar && jsvIsNumeric(countVar)) {
      int n = (int)jsvGetInteger(countVar);
      while (ok && n-- > 0) {
        ok = jsvIterateCallback(dataVar, callback, callbackData);
      }
    } else {
      jsExceptionHere(JSET_TYPEERROR, "If specifying an object, it must be of the form {data : ..., count : N} or {callback : fn} - got %j", data);
      ok = false;
    }
    jsvUnLock2(countVar, dataVar);
  }
  else if (jsvIsString(data)) {
    JsvStringIterator it;
    jsvStringIteratorNew(&it, data, 0);
    while (jsvStringIteratorHasChar(&it) && ok) {
      char ch = jsvStringIteratorGetCharAndNext(&it);
      callback(ch, callbackData);
    }
    jsvStringIteratorFree(&it);
  }
  else if (jsvIsArrayBuffer(data)) {
    JsvArrayBufferIterator it;
    jsvArrayBufferIteratorNew(&it, data, 0);
    if ((size_t)((it.type)&ARRAYBUFFERVIEW_MASK_SIZE) == 1 && !(((it.type)&ARRAYBUFFERVIEW_SIGNED)!=0)) {
      JsvStringIterator *sit = &it.it;
      size_t len = jsvGetArrayBufferLength(data);
      while (len--) {
        callback((int)(unsigned char)jsvStringIteratorGetChar(sit), callbackData);
        jsvStringIteratorNextInline(sit);
      }
    } else {
      while (jsvArrayBufferIteratorHasElement(&it)) {
        callback((int)jsvArrayBufferIteratorGetIntegerValue(&it), callbackData);
        jsvArrayBufferIteratorNext(&it);
      }
    }
    jsvArrayBufferIteratorFree(&it);
  }
  else if (jsvIsIterable(data)) {
    JsvIterator it;
    jsvIteratorNew(&it, data, JSIF_EVERY_ARRAY_ELEMENT);
    while (jsvIteratorHasElement(&it) && ok) {
      JsVar *el = jsvIteratorGetValue(&it);
      ok = jsvIterateCallback(el, callback, callbackData);
      jsvUnLock(el);
      jsvIteratorNext(&it);
    }
    jsvIteratorFree(&it);
  } else {
    jsExceptionHere(JSET_TYPEERROR, "Expecting Number or iterable, got %t", data);
    ok = false;
  }
  return ok;
}
bool jsvIterateBufferCallback(
    JsVar *data,
    jsvIterateBufferCallbackFn callback,
    void *callbackData
  ) {
  bool ok = true;
  if (jsvIsNumeric(data)) {
    unsigned char ch = (unsigned char)jsvGetInteger(data);
    callback(&ch, 1, callbackData);
  }
  else if (jsvIsObject(data)) {
    JsVar *callbackVar = jsvObjectGetChildIfExists(data, "callback");
    if (jsvIsFunction(callbackVar)) {
      JsVar *result = jspExecuteFunction(callbackVar,0,0,NULL);
      jsvUnLock(callbackVar);
      if (result) {
        bool r = jsvIterateBufferCallback(result, callback, callbackData);
        jsvUnLock(result);
        return r;
      }
      return true;
    }
    jsvUnLock(callbackVar);
    JsVar *countVar = jsvObjectGetChildIfExists(data, "count");
    JsVar *dataVar = jsvObjectGetChildIfExists(data, "data");
    if (countVar && dataVar && jsvIsNumeric(countVar)) {
      int n = (int)jsvGetInteger(countVar);
      while (ok && n-- > 0) {
        ok = jsvIterateBufferCallback(dataVar, callback, callbackData);
      }
    } else {
      jsExceptionHere(JSET_TYPEERROR, "If specifying an object, it must be of the form {data : ..., count : N} or {callback : fn} - got %j", data);
      ok = false;
    }
    jsvUnLock2(countVar, dataVar);
  }
  else if (jsvIsString(data)) {
    JsvStringIterator it;
    jsvStringIteratorNew(&it, data, 0);
    while (jsvStringIteratorHasChar(&it) && ok) {
      unsigned char *data;
      unsigned int len;
      jsvStringIteratorGetPtrAndNext(&it,&data,&len);
      callback(data, len, callbackData);
    }
    jsvStringIteratorFree(&it);
  }
  else if (jsvIsArrayBuffer(data)) {
    JsvArrayBufferIterator it;
    jsvArrayBufferIteratorNew(&it, data, 0);
    if ((size_t)((it.type)&ARRAYBUFFERVIEW_MASK_SIZE) == 1 && !(((it.type)&ARRAYBUFFERVIEW_SIGNED)!=0)) {
      JsvStringIterator *sit = &it.it;
      size_t len = jsvGetArrayBufferLength(data);
      while (len) {
        unsigned char *data;
        unsigned int dataLen;
        jsvStringIteratorGetPtrAndNext(sit,&data,&dataLen);
        if (dataLen>len) dataLen=(unsigned int)len;
        callback(data, dataLen, callbackData);
        len -= dataLen;
      }
    } else {
      while (jsvArrayBufferIteratorHasElement(&it)) {
        unsigned char ch = (unsigned char)jsvArrayBufferIteratorGetIntegerValue(&it);
        callback(&ch, 1, callbackData);
        jsvArrayBufferIteratorNext(&it);
      }
    }
    jsvArrayBufferIteratorFree(&it);
  }
  else if (jsvIsIterable(data)) {
    JsvIterator it;
    jsvIteratorNew(&it, data, JSIF_EVERY_ARRAY_ELEMENT);
    while (jsvIteratorHasElement(&it) && ok) {
      JsVar *el = jsvIteratorGetValue(&it);
      ok = jsvIterateBufferCallback(el, callback, callbackData);
      jsvUnLock(el);
      jsvIteratorNext(&it);
    }
    jsvIteratorFree(&it);
  } else {
    jsExceptionHere(JSET_TYPEERROR, "Expecting Number or iterable, got %t", data);
    ok = false;
  }
  return ok;
}
static void jsvIterateCallbackCountCb(
    unsigned char *data, unsigned int len,
    void *callbackData
  ) {
  ( (void)(data) );
  uint32_t *count = (uint32_t*)callbackData;
  (*count) += len;
}
uint32_t jsvIterateCallbackCount(JsVar *var) {
  uint32_t count = 0;
  jsvIterateBufferCallback(var, jsvIterateCallbackCountCb, (void *)&count);
  return count;
}
typedef struct { unsigned char *buf; unsigned int idx, length; } JsvIterateCallbackToBytesData;
static void jsvIterateCallbackToBytesCb(int data, void *userData) {
  JsvIterateCallbackToBytesData *cbData = (JsvIterateCallbackToBytesData*)userData;
  if (cbData->idx < cbData->length)
    cbData->buf[cbData->idx] = (unsigned char)data;
  cbData->idx++;
}
unsigned int jsvIterateCallbackToBytes(JsVar *var, unsigned char *data, unsigned int dataSize) {
  JsvIterateCallbackToBytesData cbData;
  cbData.buf = (unsigned char *)data;
  cbData.idx = 0;
  cbData.length = dataSize;
  jsvIterateCallback(var, jsvIterateCallbackToBytesCb, (void*)&cbData);
  return cbData.idx;
}
static void jsvStringIteratorCatchUp(JsvStringIterator *it) {
  while (it->charIdx>0 && it->charIdx >= it->charsInVar) {
    jsvStringIteratorLoadInline(it);
  }
}
void jsvStringIteratorNew(JsvStringIterator *it, JsVar *str, size_t startIdx) {
  do { if (!(jsvHasCharacterData(str))) jsAssertFail("bin/espruino_embedded.c",6668,""); } while(0);
    it->var = jsvLockAgain(str);
  it->varIndex = 0;
  it->charsInVar = jsvGetCharactersInVar(it->var);
  jsvStringIteratorUpdatePtr(it);
  it->charIdx = startIdx;
  jsvStringIteratorCatchUp(it);
}
void jsvStringIteratorNewUTF8(JsvStringIterator *it, JsVar *str, size_t startIdx) {
 jsvStringIteratorNew(it, str, startIdx);
}
void jsvStringIteratorUpdatePtr(JsvStringIterator *it) {
  if (jsvIsFlatString(it->var)) {
    it->ptr = jsvGetFlatStringPointer(it->var);
  } else if (jsvIsNativeString(it->var)) {
    it->ptr = (char*)it->var->varData.nativeStr.ptr;
  } else if (it->var)
    it->ptr = &it->var->varData.str[0];
  else
    it->ptr = 0;
}
void jsvStringIteratorClone(JsvStringIterator *dstit, JsvStringIterator *it) {
  *dstit = *it;
  if (dstit->var) {
    jsvLockAgain(dstit->var);
  }
}
char jsvStringIteratorGetCharAndNext(JsvStringIterator *it) {
  char ch = jsvStringIteratorGetChar(it);
  jsvStringIteratorNextInline(it);
  return ch;
}
int jsvStringIteratorGetUTF8CharAndNext(JsvStringIterator *it) {
  if (!jsvStringIteratorHasChar(it)) {
    jsvStringIteratorNext(it);
    return -1;
  }
  return (unsigned char)jsvStringIteratorGetCharAndNext(it);
}
int jsvStringIteratorGetCharOrMinusOne(JsvStringIterator *it) {
  if (!it->ptr || it->charIdx>=it->charsInVar) return -1;
  return (int)(unsigned char)(*(uint8_t*)(&it->ptr[it->charIdx]));
}
void jsvStringIteratorSetChar(JsvStringIterator *it, char c) {
  if (jsvStringIteratorHasChar(it))
    it->ptr[it->charIdx] = c;
}
void jsvStringIteratorSetCharAndNext(JsvStringIterator *it, char c) {
  if (jsvStringIteratorHasChar(it))
    it->ptr[it->charIdx] = c;
  jsvStringIteratorNextInline(it);
}
void jsvStringIteratorNext(JsvStringIterator *it) {
  jsvStringIteratorNextInline(it);
}
void jsvStringIteratorNextUTF8(JsvStringIterator *it) {
  return jsvStringIteratorNext(it);
}
void jsvStringIteratorGetPtrAndNext(JsvStringIterator *it, unsigned char **data, unsigned int *len) {
  do { if (!(jsvStringIteratorHasChar(it))) jsAssertFail("bin/espruino_embedded.c",6811,""); } while(0);
  *data = (unsigned char *)&it->ptr[it->charIdx];
  *len = (unsigned int)(it->charsInVar - it->charIdx);
  it->charIdx = it->charsInVar - 1;
  jsvStringIteratorNextInline(it);
}
void jsvStringIteratorGotoEnd(JsvStringIterator *it) {
  do { if (!(it->var)) jsAssertFail("bin/espruino_embedded.c",6819,""); } while(0);
  while (jsvGetLastChild(it->var)) {
    JsVar *next = jsvLock(jsvGetLastChild(it->var));
    jsvUnLock(it->var);
    it->var = next;
    it->varIndex += it->charsInVar;
    it->charsInVar = jsvGetCharactersInVar(it->var);
  }
  it->ptr = &it->var->varData.str[0];
  if (it->charsInVar) it->charIdx = it->charsInVar-1;
  else it->charIdx = 0;
}
void jsvStringIteratorGoto(JsvStringIterator *it, JsVar *str, size_t idx) {
  if (idx>=it->varIndex) {
    it->charIdx = idx - it->varIndex;
    jsvStringIteratorCatchUp(it);
  } else {
    jsvStringIteratorFree(it);
    jsvStringIteratorNew(it, str, idx);
  }
}
void jsvStringIteratorGotoUTF8(JsvStringIterator *it, JsVar *str, size_t idx) {
  jsvStringIteratorGoto(it, str, idx);
}
void jsvStringIteratorAppend(JsvStringIterator *it, char ch) {
  if (!it->var) return;
  if (it->charsInVar>0) {
    do { if (!(it->charIdx+1 == it->charsInVar)) jsAssertFail("bin/espruino_embedded.c",6862,""); } while(0);
    it->charIdx++;
  } else
    do { if (!(it->charIdx == 0)) jsAssertFail("bin/espruino_embedded.c",6865,""); } while(0);
  if (it->charIdx >= jsvGetMaxCharactersInVar(it->var)) {
    do { if (!(jsvHasStringExt(it->var))) jsAssertFail("bin/espruino_embedded.c",6871,""); } while(0);
    if (!jsvHasStringExt(it->var)) return;
    do { if (!(!jsvGetLastChild(it->var))) jsAssertFail("bin/espruino_embedded.c",6873,""); } while(0);
    JsVar *next = jsvNewWithFlags(JSV_STRING_EXT_0);
    if (!next) {
      jsvUnLock(it->var);
      it->var = 0;
      it->ptr = 0;
      it->charIdx = 0;
      return;
    }
    jsvSetLastChild(it->var, jsvGetRef(next));
    jsvUnLock(it->var);
    it->var = next;
    it->ptr = &next->varData.str[0];
    it->varIndex += it->charIdx;
    it->charIdx = 0;
  }
  it->ptr[it->charIdx] = ch;
  it->charsInVar = it->charIdx+1;
  jsvSetCharactersInVar(it->var, it->charsInVar);
}
void jsvStringIteratorAppendString(JsvStringIterator *it, JsVar *str, size_t startIdx, int maxLength) {
  JsvStringIterator sit;
  jsvStringIteratorNew(&sit, str, startIdx);
  while (jsvStringIteratorHasChar(&sit) && maxLength>0) {
    jsvStringIteratorAppend(it, jsvStringIteratorGetCharAndNext(&sit));
    maxLength--;
  }
  jsvStringIteratorFree(&sit);
}
void jsvObjectIteratorNew(JsvObjectIterator *it, JsVar *obj) {
  do { if (!(!obj || jsvHasChildren(obj))) jsAssertFail("bin/espruino_embedded.c",6908,""); } while(0);
  it->var = jsvHasChildren(obj) ? jsvLockSafe(jsvGetFirstChild(obj)) : 0;
}
void jsvObjectIteratorClone(JsvObjectIterator *dstit, JsvObjectIterator *it) {
  *dstit = *it;
  jsvLockAgainSafe(dstit->var);
}
void jsvObjectIteratorNext(JsvObjectIterator *it) {
  if (it->var) {
    JsVarRef next = jsvGetNextSibling(it->var);
    jsvUnLock(it->var);
    it->var = jsvLockSafe(next);
  }
}
void jsvObjectIteratorSetValue(JsvObjectIterator *it, JsVar *value) {
  if (!it->var) return;
  jsvSetValueOfName(it->var, value);
}
void jsvObjectIteratorRemoveAndGotoNext(JsvObjectIterator *it, JsVar *parent) {
  if (it->var) {
    JsVarRef next = jsvGetNextSibling(it->var);
    jsvRemoveChildAndUnLock(parent, it->var);
    it->var = jsvLockSafe(next);
  }
}
void jsvArrayBufferIteratorNew(JsvArrayBufferIterator *it, JsVar *arrayBuffer, size_t index) {
  do { if (!(jsvIsArrayBuffer(arrayBuffer))) jsAssertFail("bin/espruino_embedded.c",6942,""); } while(0);
  it->index = index;
  it->type = arrayBuffer->varData.arraybuffer.type;
  it->byteLength = arrayBuffer->varData.arraybuffer.length * (size_t)((it->type)&ARRAYBUFFERVIEW_MASK_SIZE);
  it->byteOffset = arrayBuffer->varData.arraybuffer.byteOffset;
  JsVar *arrayBufferData = jsvGetArrayBufferBackingString(arrayBuffer, NULL);
  it->byteLength += it->byteOffset;
  it->byteOffset = it->byteOffset + index*(size_t)((it->type)&ARRAYBUFFERVIEW_MASK_SIZE);
  if (it->byteOffset>=(it->byteLength+1-(size_t)((it->type)&ARRAYBUFFERVIEW_MASK_SIZE))) {
    jsvUnLock(arrayBufferData);
    it->type = ARRAYBUFFERVIEW_UNDEFINED;
    return;
  }
  jsvStringIteratorNew(&it->it, arrayBufferData, (size_t)it->byteOffset);
  jsvUnLock(arrayBufferData);
  it->hasAccessedElement = false;
}
              void jsvArrayBufferIteratorClone(JsvArrayBufferIterator *dstit, JsvArrayBufferIterator *it) {
  *dstit = *it;
  jsvStringIteratorClone(&dstit->it, &it->it);
}
static void jsvArrayBufferIteratorGetValueData(JsvArrayBufferIterator *it, char *data) {
  if (it->type == ARRAYBUFFERVIEW_UNDEFINED) return;
  do { if (!(!it->hasAccessedElement)) jsAssertFail("bin/espruino_embedded.c",6969,""); } while(0);
  int i,dataLen = (int)(size_t)((it->type)&ARRAYBUFFERVIEW_MASK_SIZE);
  for (i=0;i<dataLen;i++) {
    data[i] = jsvStringIteratorGetChar(&it->it);
    if (dataLen!=1) jsvStringIteratorNext(&it->it);
  }
  if (dataLen!=1) it->hasAccessedElement = true;
}
static JsVarInt jsvArrayBufferIteratorDataToInt(JsvArrayBufferIterator *it, char *data) {
  unsigned int dataLen = (size_t)((it->type)&ARRAYBUFFERVIEW_MASK_SIZE);
  unsigned int bits = 8*dataLen;
  JsVarInt mask = (JsVarInt)((1ULL << bits)-1);
  JsVarInt v = *(int*)data;
  v = v & mask;
  if ((((it->type)&ARRAYBUFFERVIEW_SIGNED)!=0) && (v&(JsVarInt)(1UL<<(bits-1))))
    v |= ~mask;
  return v;
}
static JsVarFloat jsvArrayBufferIteratorDataToFloat(JsvArrayBufferIterator *it, char *data) {
  unsigned int dataLen = (size_t)((it->type)&ARRAYBUFFERVIEW_MASK_SIZE);
  JsVarFloat v = 0;
  if (dataLen==4) v = *(float*)data;
  else if (dataLen==8) v = *(double*)data;
  else do { if (!(0)) jsAssertFail("bin/espruino_embedded.c",6994,""); } while(0);
  return v;
}
JsVar *jsvArrayBufferIteratorGetValue(JsvArrayBufferIterator *it, bool bigEndian) {
  if (it->type == ARRAYBUFFERVIEW_UNDEFINED) return 0;
  char data[8] __attribute__ ((aligned (4)));
  jsvArrayBufferIteratorGetValueData(it, data);
  if (bigEndian)
      reverseBytes(data, (size_t)((it->type)&ARRAYBUFFERVIEW_MASK_SIZE));
  if ((((it->type)&ARRAYBUFFERVIEW_FLOAT)!=0)) {
    return jsvNewFromFloat(jsvArrayBufferIteratorDataToFloat(it, data));
  } else {
    JsVarInt i = jsvArrayBufferIteratorDataToInt(it, data);
    if (it->type == ARRAYBUFFERVIEW_UINT32)
      return jsvNewFromLongInteger((long long)(uint32_t)i);
    return jsvNewFromInteger(i);
  }
}
JsVar *jsvArrayBufferIteratorGetValueAndRewind(JsvArrayBufferIterator *it) {
  JsvStringIterator oldIt;
  jsvStringIteratorClone(&oldIt, &it->it);
  JsVar *v = jsvArrayBufferIteratorGetValue(it, false );
  jsvStringIteratorFree(&it->it);
  it->it = oldIt;
  it->hasAccessedElement = false;
  return v;
}
JsVarInt jsvArrayBufferIteratorGetIntegerValue(JsvArrayBufferIterator *it) {
  if (it->type == ARRAYBUFFERVIEW_UNDEFINED) return 0;
  char data[8] __attribute__ ((aligned (4)));
  jsvArrayBufferIteratorGetValueData(it, data);
  if ((((it->type)&ARRAYBUFFERVIEW_FLOAT)!=0)) {
    return (JsVarInt)jsvArrayBufferIteratorDataToFloat(it, data);
  } else {
    return jsvArrayBufferIteratorDataToInt(it, data);
  }
}
JsVarFloat jsvArrayBufferIteratorGetFloatValue(JsvArrayBufferIterator *it) {
  if (it->type == ARRAYBUFFERVIEW_UNDEFINED) return 0;
  char data[8] __attribute__ ((aligned (4)));
  jsvArrayBufferIteratorGetValueData(it, data);
  if ((((it->type)&ARRAYBUFFERVIEW_FLOAT)!=0)) {
    return jsvArrayBufferIteratorDataToFloat(it, data);
  } else {
    return (JsVarFloat)jsvArrayBufferIteratorDataToInt(it, data);
  }
}
static void jsvArrayBufferIteratorIntToData(char *data, unsigned int dataLen, int type, JsVarInt v) {
  if ((((type)&ARRAYBUFFERVIEW_CLAMPED)!=0)) {
    do { if (!(dataLen==1 && !(((type)&ARRAYBUFFERVIEW_SIGNED)!=0))) jsAssertFail("bin/espruino_embedded.c",7048,""); } while(0);
    if (v<0) v=0;
    if (v>255) v=255;
  }
  if (dataLen==8) *(long long*)data = (long long)v;
  else *(int*)data = (int)v;
}
static void jsvArrayBufferIteratorFloatToData(char *data, unsigned int dataLen, int type, JsVarFloat v) {
  ( (void)(type) );
  if (dataLen==4) { *(float*)data = (float)v; }
  else if (dataLen==8) { *(double*)data = (double)v; }
  else do { if (!(0)) jsAssertFail("bin/espruino_embedded.c",7061,""); } while(0);
}
void jsvArrayBufferIteratorSetIntegerValue(JsvArrayBufferIterator *it, JsVarInt v) {
  if (it->type == ARRAYBUFFERVIEW_UNDEFINED) return;
  do { if (!(!it->hasAccessedElement)) jsAssertFail("bin/espruino_embedded.c",7066,""); } while(0);
  char data[8] __attribute__ ((aligned (4)));
  unsigned int i,dataLen = (size_t)((it->type)&ARRAYBUFFERVIEW_MASK_SIZE);
  if ((((it->type)&ARRAYBUFFERVIEW_FLOAT)!=0)) {
    jsvArrayBufferIteratorFloatToData(data, dataLen, it->type, (JsVarFloat)v);
  } else {
    jsvArrayBufferIteratorIntToData(data, dataLen, it->type, v);
  }
  for (i=0;i<dataLen;i++) {
    jsvStringIteratorSetChar(&it->it, data[i]);
    if (dataLen!=1) jsvStringIteratorNext(&it->it);
  }
  if (dataLen!=1) it->hasAccessedElement = true;
}
void jsvArrayBufferIteratorSetValue(JsvArrayBufferIterator *it, JsVar *value, bool bigEndian) {
  if (it->type == ARRAYBUFFERVIEW_UNDEFINED) return;
  do { if (!(!it->hasAccessedElement)) jsAssertFail("bin/espruino_embedded.c",7085,""); } while(0);
  char data[8] __attribute__ ((aligned (4)));
  int i,dataLen = (int)(size_t)((it->type)&ARRAYBUFFERVIEW_MASK_SIZE);
  if ((((it->type)&ARRAYBUFFERVIEW_FLOAT)!=0)) {
    jsvArrayBufferIteratorFloatToData(data, (unsigned)dataLen, it->type, jsvGetFloat(value));
  } else {
    jsvArrayBufferIteratorIntToData(data, (unsigned)dataLen, it->type, jsvGetInteger(value));
  }
  if (bigEndian)
    reverseBytes(data, dataLen);
  for (i=0;i<dataLen;i++) {
    jsvStringIteratorSetChar(&it->it, data[i]);
    if (dataLen!=1) jsvStringIteratorNext(&it->it);
  }
  if (dataLen!=1) it->hasAccessedElement = true;
}
void jsvArrayBufferIteratorSetByteValue(JsvArrayBufferIterator *it, char c) {
  if ((size_t)((it->type)&ARRAYBUFFERVIEW_MASK_SIZE)!=1) {
    do { if (!(0)) jsAssertFail("bin/espruino_embedded.c",7107,""); } while(0);
    return;
  }
  jsvStringIteratorSetChar(&it->it, c);
}
void jsvArrayBufferIteratorSetValueAndRewind(JsvArrayBufferIterator *it, JsVar *value) {
  JsvStringIterator oldIt;
  jsvStringIteratorClone(&oldIt, &it->it);
  jsvArrayBufferIteratorSetValue(it, value, false );
  jsvStringIteratorFree(&it->it);
  jsvStringIteratorClone(&it->it, &oldIt);
  jsvStringIteratorFree(&oldIt);
  it->hasAccessedElement = false;
}
JsVar* jsvArrayBufferIteratorGetIndex(JsvArrayBufferIterator *it) {
  return jsvNewFromInteger((JsVarInt)it->index);
}
bool jsvArrayBufferIteratorHasElement(JsvArrayBufferIterator *it) {
  if (it->type == ARRAYBUFFERVIEW_UNDEFINED) return false;
  if (it->hasAccessedElement) return true;
  return (it->byteOffset+(size_t)((it->type)&ARRAYBUFFERVIEW_MASK_SIZE)) <= it->byteLength;
}
void jsvArrayBufferIteratorNext(JsvArrayBufferIterator *it) {
  it->index++;
  it->byteOffset += (size_t)((it->type)&ARRAYBUFFERVIEW_MASK_SIZE);
  if (!it->hasAccessedElement) {
    unsigned int dataLen = (size_t)((it->type)&ARRAYBUFFERVIEW_MASK_SIZE);
    while (dataLen--)
      jsvStringIteratorNext(&it->it);
  } else
    it->hasAccessedElement = false;
}
void jsvArrayBufferIteratorFree(JsvArrayBufferIterator *it) {
  if (it->type == ARRAYBUFFERVIEW_UNDEFINED) return;
  jsvStringIteratorFree(&it->it);
}
void jsvIteratorNew(JsvIterator *it, JsVar *obj, JsvIteratorFlags flags) {
  if (jsvIsArray(obj) || jsvIsObject(obj) || jsvIsFunction(obj) || jsvIsGetterOrSetter(obj)) {
    it->type = JSVI_OBJECT;
    if (jsvIsArray(obj) && (flags&JSIF_EVERY_ARRAY_ELEMENT)) {
      it->type = JSVI_FULLARRAY;
      it->it.obj.index = 0;
      it->it.obj.var = jsvLockAgain(obj);
    }
    jsvObjectIteratorNew(&it->it.obj.it, obj);
  } else if (jsvIsArrayBuffer(obj)) {
    it->type = JSVI_ARRAYBUFFER;
    jsvArrayBufferIteratorNew(&it->it.buf, obj, 0);
  } else if (jsvHasCharacterData(obj)) {
    it->type = JSVI_STRING;
    jsvStringIteratorNew(&it->it.str, obj, 0);
  } else {
    it->type = JSVI_NONE;
    do { if (!(0)) jsAssertFail("bin/espruino_embedded.c",7188,""); } while(0);
  }
}
JsVar *jsvIteratorGetKey(JsvIterator *it) {
  switch (it->type) {
  case JSVI_FULLARRAY : return jsvNewFromInteger(it->it.obj.index);
  case JSVI_OBJECT : return jsvObjectIteratorGetKey(&it->it.obj.it);
  case JSVI_STRING : return jsvMakeIntoVariableName(jsvNewFromInteger((JsVarInt)jsvStringIteratorGetIndex(&it->it.str)), 0);
  case JSVI_ARRAYBUFFER : return jsvMakeIntoVariableName(jsvArrayBufferIteratorGetIndex(&it->it.buf), 0);
  default: do { if (!(0)) jsAssertFail("bin/espruino_embedded.c",7202,""); } while(0); return 0;
  }
}
JsVar *jsvIteratorGetValue(JsvIterator *it) {
  switch (it->type) {
  case JSVI_FULLARRAY: if (jsvIsIntegerish(it->it.obj.it.var) &&
                           jsvGetInteger(it->it.obj.it.var) == it->it.obj.index)
                         return jsvObjectIteratorGetValue(&it->it.obj.it);
                       return 0;
  case JSVI_OBJECT : return jsvObjectIteratorGetValue(&it->it.obj.it);
  case JSVI_STRING : { char buf = jsvStringIteratorGetChar(&it->it.str); return jsvNewStringOfLength(1, &buf); }
  case JSVI_ARRAYBUFFER : return jsvArrayBufferIteratorGetValueAndRewind(&it->it.buf);
  default: do { if (!(0)) jsAssertFail("bin/espruino_embedded.c",7222,""); } while(0); return 0;
  }
}
JsVarInt jsvIteratorGetIntegerValue(JsvIterator *it) {
  switch (it->type) {
  case JSVI_FULLARRAY: {
    if (jsvIsNameInt(it->it.obj.it.var) &&
        jsvGetInteger(it->it.obj.it.var) == it->it.obj.index)
      return (JsVarInt)jsvGetFirstChildSigned(it->it.obj.it.var);
    if (jsvIsIntegerish(it->it.obj.it.var) &&
        jsvGetInteger(it->it.obj.it.var) == it->it.obj.index)
      return jsvGetIntegerAndUnLock(jsvObjectIteratorGetValue(&it->it.obj.it));
    return 0;
  }
  case JSVI_OBJECT : {
    if (jsvIsNameInt(it->it.obj.it.var))
      return (JsVarInt)jsvGetFirstChildSigned(it->it.obj.it.var);
    return jsvGetIntegerAndUnLock(jsvObjectIteratorGetValue(&it->it.obj.it));
  }
  case JSVI_STRING : return (JsVarInt)jsvStringIteratorGetChar(&it->it.str);
  case JSVI_ARRAYBUFFER : return jsvArrayBufferIteratorGetIntegerValue(&it->it.buf);
  default: do { if (!(0)) jsAssertFail("bin/espruino_embedded.c",7248,""); } while(0); return 0;
  }
}
JsVarFloat jsvIteratorGetFloatValue(JsvIterator *it) {
  switch (it->type) {
  case JSVI_FULLARRAY: if (jsvIsIntegerish(it->it.obj.it.var) &&
                           jsvGetInteger(it->it.obj.it.var) == it->it.obj.index)
                         return jsvGetFloatAndUnLock(jsvObjectIteratorGetValue(&it->it.obj.it));
                       return NAN;
  case JSVI_OBJECT : return jsvGetFloatAndUnLock(jsvObjectIteratorGetValue(&it->it.obj.it));
  case JSVI_ARRAYBUFFER : return jsvArrayBufferIteratorGetFloatValue(&it->it.buf);
  default: return (JsVarFloat)jsvIteratorGetIntegerValue(it);
  }
}
JsVar *jsvIteratorSetValue(JsvIterator *it, JsVar *value) {
  switch (it->type) {
  case JSVI_FULLARRAY: if (jsvIsIntegerish(it->it.obj.it.var) &&
                             jsvGetInteger(it->it.obj.it.var) == it->it.obj.index)
                          jsvObjectIteratorSetValue(&it->it.obj.it, value);
                        jsvSetArrayItem(it->it.obj.var, it->it.obj.index, value);
                        break;
  case JSVI_OBJECT : jsvObjectIteratorSetValue(&it->it.obj.it, value); break;
  case JSVI_STRING : jsvStringIteratorSetChar(&it->it.str, (char)(jsvIsString(value) ? value->varData.str[0] : (char)jsvGetInteger(value))); break;
  case JSVI_ARRAYBUFFER : jsvArrayBufferIteratorSetValueAndRewind(&it->it.buf, value); break;
  default: do { if (!(0)) jsAssertFail("bin/espruino_embedded.c",7277,""); } while(0); break;
  }
  return value;
}
bool jsvIteratorHasElement(JsvIterator *it) {
  switch (it->type) {
  case JSVI_FULLARRAY: return it->it.obj.index < jsvGetArrayLength(it->it.obj.var);
  case JSVI_OBJECT : return jsvObjectIteratorHasValue(&it->it.obj.it);
  case JSVI_STRING : return jsvStringIteratorHasChar(&it->it.str);
  case JSVI_ARRAYBUFFER : return jsvArrayBufferIteratorHasElement(&it->it.buf);
  default: do { if (!(0)) jsAssertFail("bin/espruino_embedded.c",7291,""); } while(0); return 0;
  }
}
void jsvIteratorNext(JsvIterator *it) {
  switch (it->type) {
  case JSVI_FULLARRAY: it->it.obj.index++;
                       if (jsvIsIntegerish(it->it.obj.it.var) &&
                           jsvGetInteger(it->it.obj.it.var)<it->it.obj.index)
                         jsvObjectIteratorNext(&it->it.obj.it);
                       break;
  case JSVI_OBJECT : jsvObjectIteratorNext(&it->it.obj.it); break;
  case JSVI_STRING : jsvStringIteratorNext(&it->it.str); break;
  case JSVI_ARRAYBUFFER : jsvArrayBufferIteratorNext(&it->it.buf); break;
  default: do { if (!(0)) jsAssertFail("bin/espruino_embedded.c",7308,""); } while(0); break;
  }
}
void jsvIteratorFree(JsvIterator *it) {
  switch (it->type) {
  case JSVI_FULLARRAY: jsvUnLock(it->it.obj.var);
                       jsvObjectIteratorFree(&it->it.obj.it);
       break;
  case JSVI_OBJECT : jsvObjectIteratorFree(&it->it.obj.it); break;
  case JSVI_STRING : jsvStringIteratorFree(&it->it.str); break;
  case JSVI_ARRAYBUFFER : jsvArrayBufferIteratorFree(&it->it.buf); break;
  default: do { if (!(0)) jsAssertFail("bin/espruino_embedded.c",7323,""); } while(0); break;
  }
}
void jsvIteratorClone(JsvIterator *dstit, JsvIterator *it) {
  dstit->type = it->type;
  switch (it->type) {
  case JSVI_FULLARRAY: dstit->it.obj.index = it->it.obj.index;
                       dstit->it.obj.var = jsvLockAgain(it->it.obj.var);
                       jsvObjectIteratorClone(&dstit->it.obj.it, &it->it.obj.it);
                       break;
  case JSVI_OBJECT : jsvObjectIteratorClone(&dstit->it.obj.it, &it->it.obj.it); break;
  case JSVI_STRING : jsvStringIteratorClone(&dstit->it.str, &it->it.str); break;
  case JSVI_ARRAYBUFFER : jsvArrayBufferIteratorClone(&dstit->it.buf, &it->it.buf); break;
  default: do { if (!(0)) jsAssertFail("bin/espruino_embedded.c",7343,""); } while(0); break;
  }
}
JsVar *jswrap_error_constructor(JsVar *msg);
JsVar *jswrap_syntaxerror_constructor(JsVar *msg);
JsVar *jswrap_typeerror_constructor(JsVar *msg);
JsVar *jswrap_internalerror_constructor(JsVar *msg);
JsVar *jswrap_referenceerror_constructor(JsVar *msg);
JsVar *jswrap_error_toString(JsVar *parent);
volatile JsErrorFlags jsErrorFlags;
bool isWhitespace(char ch) {
    return isWhitespaceInline(ch);
}
bool isHexadecimal(char ch) {
    return ((ch>='0') && (ch<='9')) ||
           ((ch>='a') && (ch<='f')) ||
           ((ch>='A') && (ch<='F'));
}
bool isAlpha(char ch) {
    return isAlphaInline(ch);
}
bool isNumeric(char ch) {
    return isNumericInline(ch);
}
bool isIDString(const char *s) {
  if (!isAlpha(*s))
    return false;
  while (*s) {
    if (!(isAlpha(*s) || isNumeric(*s)))
      return false;
    s++;
  }
  return true;
}
char dtohex(int d) {
  d &= 15;
  return (char)((d<10)?('0'+d):('A'+d-10));
}
char charToUpperCase(char ch) {
  return (char)(((ch>=97 && ch<=122) ||
    ((unsigned)ch>=224 && (unsigned)ch<=246) ||
    ((unsigned)ch>=248 && (unsigned)ch<=254)) ? ch - 32 : ch);
}
char charToLowerCase(char ch) {
  return (char)(((ch>=65 && ch<=90) ||
    ((unsigned)ch>=192 && (unsigned)ch<=214) ||
    ((unsigned)ch>=216 && (unsigned)ch<=222)) ? ch + 32 : ch);
}
static char* numericEscapeChar(char *dst, int ch, bool useUnicode) {
  *(dst++) = '\\';
  if (useUnicode) {
    *(dst++) ='u';
    *(dst++) = dtohex(ch>>12);
    *(dst++) = dtohex(ch>>8);
  } else {
    *(dst++) = 'x';
  }
  *(dst++) = dtohex(ch>>4);
  *(dst++) = dtohex(ch);
  return dst;
}
const char *escapeCharacter(int ch, int nextCh, bool jsonStyle) {
  if (ch=='\n') return "\\n";
  if (ch=='\t') return "\\t";
  if (ch=='\b') return "\\b";
  if (ch=='\v' && !jsonStyle) return "\\v";
  if (ch=='\f') return "\\f";
  if (ch=='\r') return "\\r";
  if (ch=='\\') return "\\\\";
  if (ch=='"') return "\\\"";
  static char buf[14];
  if (ch<8 && !jsonStyle && (nextCh<'0' || nextCh>'7')) {
    buf[0]='\\';
    buf[1] = (char)('0'+ch);
    buf[2] = 0;
    return buf;
  } else
  if (ch<32 || ch>=127) {
    char *p = buf;
    p = numericEscapeChar(p, ch, jsonStyle || ch>255);
    *p = 0;
    return buf;
  }
  buf[1] = 0;
  buf[0] = (char)ch;
  return buf;
}
__attribute__ ((noinline)) int getRadix(const char **s) {
  int radix = 10;
  if (**s == '0') {
    radix = 8;
    (*s)++;
    if (**s == 'o' || **s == 'O') {
      radix = 8;
      (*s)++;
    } else if (**s == 'x' || **s == 'X') {
      radix = 16;
      (*s)++;
    } else if (**s == 'b' || **s == 'B') {
      radix = 2;
      (*s)++;
    } else {
      const char *p;
      for (p=*s;*p;p++)
        if (*p=='.' || *p=='8' || *p=='9')
           radix = 10;
        else if (*p<'0' || *p>'9') break;
    }
  }
  return radix;
}
int chtod(char ch) {
  if (ch >= '0' && ch <= '9')
    return ch - '0';
  else if (ch >= 'a' && ch <= 'z')
    return 10 + ch - 'a';
  else if (ch >= 'A' && ch <= 'Z')
    return 10 + ch - 'A';
  else return -1;
}
int hexToByte(char hi, char lo) {
  int a = chtod(hi);
  int b = chtod(lo);
  if (a<0 || b<0) return -1;
  return (a<<4)|b;
}
long long stringToIntWithRadix(const char *s,
               int forceRadix,
               bool *hasError,
               const char **endOfInteger
               ) {
  while (isWhitespace(*s)) s++;
  bool isNegated = false;
  long long v = 0;
  if (*s == '-') {
    isNegated = true;
    s++;
  } else if (*s == '+') {
    s++;
  }
  const char *numberStart = s;
  if (endOfInteger) (*endOfInteger)=s;
  int radix = forceRadix ? forceRadix : getRadix(&s);
  if (!radix) return 0;
  while (*s) {
    int digit = chtod(*s);
    if (digit<0 || digit>=radix)
      break;
    v = v*radix + digit;
    s++;
  }
  if (hasError)
    *hasError = s==numberStart;
  if (endOfInteger) (*endOfInteger)=s;
  if (isNegated) return -v;
  return v;
}
long long stringToInt(const char *s) {
  return stringToIntWithRadix(s,0,NULL,NULL);
}
__attribute__ ((noinline)) void jsError(const char *fmt, ...) {
  jsiConsoleRemoveInputLine();
  jsiConsolePrintString("ERROR: ");
  va_list argp;
  va_start(argp, fmt);
  vcbprintf(vcbprintf_callback_jsiConsolePrintString,0, fmt, argp);
  va_end(argp);
  jsiConsolePrintString("\n");
}
__attribute__ ((noinline)) void jsWarn(const char *fmt, ...) {
  jsiConsoleRemoveInputLine();
  jsiConsolePrintString("WARNING: ");
  va_list argp;
  va_start(argp, fmt);
  vcbprintf(vcbprintf_callback_jsiConsolePrintString,0, fmt, argp);
  va_end(argp);
  jsiConsolePrintString("\n");
}
__attribute__ ((noinline)) void jsExceptionHere(JsExceptionType type, const char *fmt, ...) {
  if (jspHasError()) return;
  jsiConsoleRemoveInputLine();
  JsVar *var = jsvNewFromEmptyString();
  if (!var) {
    jspSetError();
    return;
  }
  JsvStringIterator it;
  jsvStringIteratorNew(&it, var, 0);
  jsvStringIteratorGotoEnd(&it);
  vcbprintf_callback cb = (vcbprintf_callback)jsvStringIteratorPrintfCallback;
  va_list argp;
  va_start(argp, fmt);
  vcbprintf(cb,&it, fmt, argp);
  va_end(argp);
  jsvStringIteratorFree(&it);
  if (type != JSET_STRING) {
    JsVar *obj = 0;
    if (type == JSET_ERROR) obj = jswrap_error_constructor(var);
    else if (type == JSET_SYNTAXERROR) obj = jswrap_syntaxerror_constructor(var);
    else if (type == JSET_TYPEERROR) obj = jswrap_typeerror_constructor(var);
    else if (type == JSET_INTERNALERROR) obj = jswrap_internalerror_constructor(var);
    else if (type == JSET_REFERENCEERROR) obj = jswrap_referenceerror_constructor(var);
    jsvUnLock(var);
    var = obj;
  }
  jspSetException(var);
  jsvUnLock(var);
}
__attribute__ ((noinline)) void jsAssertFail(const char *file, int line, const char *expr) {
  static bool inAssertFail = false;
  bool wasInAssertFail = inAssertFail;
  inAssertFail = true;
  jsiConsoleRemoveInputLine();
  if (expr) {
    jsiConsolePrintf("ASSERT(%s) FAILED AT ", expr);
  } else {
    jsiConsolePrintString("ASSERT FAILED AT ");
  }
  jsiConsolePrintf("%s:%d\n",file,line);
  if (!wasInAssertFail) {
    jsvTrace(jsvFindOrCreateRoot(), 2);
  }
  jsiConsolePrintString("HALTING.\n");
  while (1);
  inAssertFail = false;
}
JsVarFloat stringToFloatWithRadix(
    const char *s,
   int forceRadix,
   const char **endOfFloat
  ) {
  while (isWhitespace(*s)) s++;
  bool isNegated = false;
  if (*s == '-') {
    isNegated = true;
    s++;
  } else if (*s == '+') {
    s++;
  }
  const char *numberStart = s;
  if (endOfFloat) (*endOfFloat)=s;
  int radix = forceRadix ? forceRadix : getRadix(&s);
  if (!radix) return NAN;
  JsVarFloat v = 0;
  JsVarFloat mul = 0.1;
  while (*s) {
    int digit = chtod(*s);
    if (digit<0 || digit>=radix)
      break;
    v = (v*radix) + digit;
    s++;
  }
  if (radix == 10) {
    if (*s == '.') {
      s++;
      while (*s) {
        if (*s >= '0' && *s <= '9')
          v += mul*(*s - '0');
        else break;
        mul /= 10;
        s++;
      }
    }
    if (*s == 'e' || *s == 'E') {
      s++;
      bool isENegated = false;
      if (*s == '-' || *s == '+') {
        isENegated = *s=='-';
        s++;
      }
      int e = 0;
      while (*s) {
        if (*s >= '0' && *s <= '9')
          e = (e*10) + (*s - '0');
        else break;
        s++;
      }
      if (isENegated) e=-e;
      while (e>0) {
        v*=10;
        e--;
      }
      while (e<0) {
        v/=10;
        e++;
      }
    }
  }
  if (endOfFloat) (*endOfFloat)=s;
  if (numberStart==s ||
      (numberStart[0]=='.' && s==&numberStart[1])
      ) return NAN;
  if (isNegated) return -v;
  return v;
}
JsVarFloat stringToFloat(const char *s) {
  return stringToFloatWithRadix(s, 0, NULL);
}
char itoch(int val) {
  if (val<10) return (char)('0'+val);
  return (char)('a'+val-10);
}
void itostr_extra(JsVarInt vals,char *str,bool signedVal, unsigned int base) {
  JsVarIntUnsigned val;
  if (signedVal && vals<0) {
    *(str++)='-';
    val = (JsVarIntUnsigned)(-vals);
  } else {
    val = (JsVarIntUnsigned)vals;
  }
  JsVarIntUnsigned tmp = val;
  int digits = 1;
  while (tmp>=base) {
    digits++;
    tmp /= base;
  }
  int i;
  for (i=digits-1;i>=0;i--) {
    str[i] = itoch((int)(val % base));
    val /= base;
  }
  str[digits] = 0;
}
void ftoa_bounded_extra(JsVarFloat val,char *str, size_t len, int radix, int fractionalDigits) {
  do { if (!(len>9)) jsAssertFail("bin/espruino_embedded.c",7988,""); } while(0);
  const JsVarFloat stopAtError = 0.0000001;
  if (isnan(val)) strcpy(str,"NaN");
  else if (!isfinite(val)) {
    if (val<0) strcpy(str,"-Infinity");
    else strcpy(str,"Infinity");
  } else {
    if (val<0) {
      if (--len <= 0) { *str=0; return; }
      *(str++) = '-';
      val = -val;
    }
    int exponent = 0;
    if (radix == 10 && val>0.0 && fractionalDigits<0) {
      if (val >= 1E21) {
        while (val>100000) {
          val /= 100000;
          exponent += 5;
        }
        while (val>10) {
          val /= 10;
          exponent ++;
        }
      } else if (val < 1E-6) {
        while (val<1E-5) {
          val *= 100000;
          exponent -= 5;
        }
        while (val<1) {
          val *= 10;
          exponent --;
        }
      }
    }
    if (((JsVarInt)(val+stopAtError)) == (1+(JsVarInt)val))
      val = (JsVarFloat)(1+(JsVarInt)val);
    JsVarFloat d = 1;
    while (d*radix <= val) d*=radix;
    while (d >= 1) {
      int v = (int)(val / d);
      val -= v*d;
      if (--len <= 0) { *str=0; return; }
      *(str++) = itoch(v);
      d /= radix;
    }
    if (((fractionalDigits<0) && val>0) || fractionalDigits>0) {
      bool hasPt = false;
      val*=radix;
      while (((fractionalDigits<0) && (fractionalDigits>-12) && (val > stopAtError)) || (fractionalDigits > 0)) {
        int v = (int)(val+((fractionalDigits==1) ? 0.5 : 0.00000001) );
        val = (val-v)*radix;
 if (v==radix) v=radix-1;
        if (!hasPt) {
   hasPt = true;
          if (--len <= 0) { *str=0; return; }
          *(str++)='.';
        }
        if (--len <= 0) { *str=0; return; }
        *(str++)=itoch(v);
        fractionalDigits--;
      }
    }
    if (exponent && len > 5) {
      *str++ = 'e';
      if (exponent>0) *str++ = '+';
      itostr(exponent, str, 10);
      return;
    }
    *(str++)=0;
  }
}
void ftoa_bounded(JsVarFloat val,char *str, size_t len) {
  ftoa_bounded_extra(val, str, len, 10, -1);
}
JsVarFloat wrapAround(JsVarFloat val, JsVarFloat size) {
  if (size<0.0) return 0.0;
  val = val / size;
  val = val - (int)val;
  return val * size;
}
void vcbprintf(
    vcbprintf_callback user_callback,
    void *user_data,
    const char *fmt,
    va_list argp
  ) {
  char buf[32];
  while (*fmt) {
    if (*fmt == '%') {
      fmt++;
      char fmtChar = *fmt++;
      switch (fmtChar) {
      case ' ':
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
      {
        const char *pad = " ";
        if (!*fmt) break;
        if (fmtChar=='0') {
          pad = "0";
          fmtChar = *fmt++;
          if (!*fmt) break;
        }
        int digits = fmtChar - '0';
        int v = va_arg(argp, int);
        if (*fmt=='x') itostr_extra(v, buf, false, 16);
        else { do { if (!('d' == *fmt)) jsAssertFail("bin/espruino_embedded.c",8143,""); } while(0); itostr(v, buf, 10); }
        fmt++;
        int len = (int)strlen(buf);
        while (len < digits) {
          user_callback(pad,user_data);
          len++;
        }
        user_callback(buf,user_data);
        break;
      }
      case 'i':
      case 'd': itostr(va_arg(argp, int), buf, 10); user_callback(buf,user_data); break;
      case 'x': itostr_extra(va_arg(argp, int), buf, false, 16); user_callback(buf,user_data); break;
      case 'L': {
        unsigned int rad = 10;
        bool signedVal = true;
        if (*fmt=='x') { rad=16; fmt++; signedVal = false; }
        itostr_extra(va_arg(argp, JsVarInt), buf, signedVal, rad); user_callback(buf,user_data);
      } break;
      case 'f': ftoa_bounded(va_arg(argp, JsVarFloat), buf, sizeof(buf)); user_callback(buf,user_data); break;
      case 's': user_callback(va_arg(argp, char *), user_data); break;
      case 'c': buf[0]=(char)va_arg(argp, int );buf[1]=0; user_callback(buf, user_data); break;
      case 'q':
      case 'Q':
      case 'v': {
        bool quoted = fmtChar!='v';
        bool isJSONStyle = fmtChar=='Q';
        if (quoted) user_callback("\"",user_data);
        JsVar *v = jsvAsString(va_arg(argp, JsVar*));
        if (jsvIsUTF8String(v)) isJSONStyle=true;
        buf[1] = 0;
        if (jsvIsString(v)) {
          JsvStringIterator it;
          jsvStringIteratorNewUTF8(&it, v, 0);
          if (quoted) {
            int ch = jsvStringIteratorGetUTF8CharAndNext(&it);
            while (jsvStringIteratorHasChar(&it) || ch>=0) {
              int nextCh = jsvStringIteratorGetUTF8CharAndNext(&it);
              if (quoted) {
                user_callback(escapeCharacter(ch, nextCh, isJSONStyle), user_data);
              } else {
                user_callback(buf,user_data);
              }
              ch = nextCh;
            }
          } else {
            while (jsvStringIteratorHasChar(&it)) {
              buf[0] = jsvStringIteratorGetCharAndNext(&it);
              user_callback(buf,user_data);
            }
          }
          jsvStringIteratorFree(&it);
          jsvUnLock(v);
        }
        if (quoted) user_callback("\"",user_data);
      } break;
      case 'j': {
        JsVar *v = va_arg(argp, JsVar*);
        jsfGetJSONWithCallback(v, NULL, JSON_SOME_NEWLINES | JSON_PRETTY | JSON_SHOW_DEVICES | JSON_ALLOW_TOJSON, 0, user_callback, user_data);
        break;
      }
      case 't': {
        JsVar *v = va_arg(argp, JsVar*);
        const char *n = jsvIsNull(v)?"null":jswGetBasicObjectName(v);
        if (!n) n = jsvGetTypeOf(v);
        user_callback(n, user_data);
        break;
      }
      default: do { if (!(0)) jsAssertFail("bin/espruino_embedded.c",8214,""); } while(0); return;
      }
    } else {
      buf[0] = *(fmt++);
      buf[1] = 0;
      user_callback(&buf[0], user_data);
    }
  }
}
void cbprintf(vcbprintf_callback user_callback, void *user_data, const char *fmt, ...) {
  va_list argp;
  va_start(argp, fmt);
  vcbprintf(user_callback,user_data, fmt, argp);
  va_end(argp);
}
typedef struct {
  char *outPtr;
  size_t idx;
  size_t len;
} espruino_snprintf_data;
void espruino_snprintf_cb(const char *str, void *userdata) {
  espruino_snprintf_data *d = (espruino_snprintf_data*)userdata;
  while (*str) {
    if (d->idx < d->len) d->outPtr[d->idx] = *str;
    d->idx++;
    str++;
  }
}
int espruino_snprintf_va( char * s, size_t n, const char * fmt, va_list argp ) {
  espruino_snprintf_data d;
  d.outPtr = s;
  d.idx = 0;
  d.len = n;
  vcbprintf(espruino_snprintf_cb,&d, fmt, argp);
  if (d.idx < d.len) d.outPtr[d.idx] = 0;
  else d.outPtr[d.len-1] = 0;
  return (int)d.idx;
}
int espruino_snprintf( char * s, size_t n, const char * fmt, ... ) {
  va_list argp;
  va_start(argp, fmt);
  int l = espruino_snprintf_va(s,n,fmt,argp);
  va_end(argp);
  return l;
}
size_t jsuGetFreeStack() {
  return 1000000;
}
unsigned int rand_m_w = 0xDEADBEEF;
unsigned int rand_m_z = 0xCAFEBABE;
int rand() {
  rand_m_z = 36969 * (rand_m_z & 65535) + (rand_m_z >> 16);
  rand_m_w = 18000 * (rand_m_w & 65535) + (rand_m_w >> 16);
  return (int)RAND_MAX & (int)((rand_m_z << 16) + rand_m_w);
}
void srand(unsigned int seed) {
  rand_m_w = (seed&0xFFFF) | (seed<<16);
  rand_m_z = (seed&0xFFFF0000) | (seed>>16);
}
char clipi8(int x) {
  if (x<-128) return -128;
  if (x>127) return 127;
  return (char)x;
}
int twosComplement(int val, unsigned char bits) {
  if ((unsigned)val & ((unsigned int)1 << (bits - 1)))
    val -= 1 << bits;
  return val;
}
bool calculateParity(uint8_t v) {
  v ^= v >> 4;
  return (0x6996 >> (v&0xf)) & 1;
}
unsigned short int int_sqrt32(unsigned int x) {
  unsigned short int res=0;
  unsigned short int add= 0x8000;
  int i;
  for(i=0;i<16;i++) {
    unsigned short int temp=res | add;
    unsigned int g2=temp*temp;
    if (x>=g2)
      res=temp;
    add>>=1;
  }
  return res;
}
void reverseBytes(char *data, int len) {
  int halflen = len>>1;
  int j = len-1;
  for (int i=0;i<halflen;i++,j--) {
    char t = data[i];
    data[i] = data[j];
    data[j] = t;
  }
}
JsVar *jsnCallFunction(void *function, JsnArgumentType argumentSpecifier, JsVar *thisParam, JsVar **paramData, int paramCount) ;
void jsnSanityTest();
JsVar *jsnCallFunction(void *function, JsnArgumentType argumentSpecifier, JsVar *thisParam, JsVar **paramData, int paramCount) {
  return jswCallFunctionHack(function, argumentSpecifier, thisParam, paramData, paramCount);
}
char const sanity_test_arraybuffer_type_one_byte[sizeof(JsVarDataArrayBufferViewType) == 1 ? 1 : -1];
char const sanity_test_arraybuffer_in_jsvar[sizeof(JsVarDataArrayBufferView) <= (4 + ((14*2)>>3)) ? 1 : -1];
char const sanity_test_native_in_jsvar[sizeof(JsVarDataNative) <= (4 + ((14*2)>>3)) ? 1 : -1];
char const sanity_test_nativestr_in_jsvar[sizeof(JsVarDataNativeStr) <= (4 + ((14*3)>>3)) ? 1 : -1];
char const sanity_test_jsvarflags_is_2bytes[sizeof(JsVarFlags) == 2 ? 1 : -1];
char const sanity_test_ioeventflags_is_1byte[sizeof(IOEventFlags) == 1 ? 1 : -1];
JsVarFloat sanity_pi() { return 3.141592; }
int32_t sanity_int_pass(int32_t hello) { return (hello*10)+5; }
int32_t sanity_int_flt_int(int32_t a, JsVarFloat b, int32_t c) {
  return a + (int32_t)(b*100) + c*10000;
}
void jsnSanityTest() {
  if (sizeof(JsVarFlags)!=2)
    jsiConsolePrintf("WARNING: jsnative.c sanity check failed (sizeof(JsVarFlags)==2) %d\n", sizeof(JsVarFlags));
  if (sizeof(JsnArgumentType)!=2)
    jsiConsolePrintf("WARNING: jsnative.c sanity check failed (sizeof(JsnArgumentType)==2) %d\n", sizeof(JsnArgumentType));
  JsVar *args[4];
  if (jsvGetFloatAndUnLock(jsnCallFunction(sanity_pi, JSWAT_JSVARFLOAT, 0, 0, 0)) != 3.141592)
    jsiConsolePrintString("WARNING: jsnative.c sanity check failed (returning double values)\n");
  args[0] = jsvNewFromInteger(1234);
  if (jsvGetIntegerAndUnLock(jsnCallFunction(sanity_int_pass, JSWAT_INT32|(JSWAT_INT32<<(((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )), 0, args, 1)) != 12345)
      jsiConsolePrintString("WARNING: jsnative.c sanity check failed (simple integer passing)\n");
  jsvUnLock(args[0]);
}
JsVarFloat jswrap_espruino_getTemperature();
JsVar *jswrap_espruino_nativeCall(JsVarInt addr, JsVar *signature, JsVar *data);
JsVarFloat jswrap_espruino_clip(JsVarFloat x, JsVarFloat min, JsVarFloat max);
JsVarFloat jswrap_espruino_sum(JsVar *arr);
JsVarFloat jswrap_espruino_variance(JsVar *arr, JsVarFloat mean);
JsVarFloat jswrap_espruino_convolve(JsVar *a, JsVar *b, int offset);
void jswrap_espruino_FFT(JsVar *arrReal, JsVar *arrImag, bool inverse);
void jswrap_espruino_enableWatchdog(JsVarFloat time, JsVar *isAuto);
void jswrap_espruino_kickWatchdog();
void jswrap_espruino_setComparator_eventHandler(IOEventFlags eventFlags, uint8_t *data, int length);
void jswrap_espruino_setComparator(Pin pin, JsVarFloat level);
JsVar *jswrap_espruino_getErrorFlagArray(JsErrorFlags flags);
JsVar *jswrap_espruino_getErrorFlags();
JsVar *jswrap_espruino_toArrayBuffer(JsVar *str);
JsVar *jswrap_espruino_toUint8Array(JsVar *args);
JsVar *jswrap_espruino_toString(JsVar *args);
JsVar *jswrap_espruino_toFlatString(JsVar *args);
JsVar *jswrap_espruino_asUTF8(JsVar *str);
JsVar *jswrap_espruino_fromUTF8(JsVar *str);
bool jswrap_espruino_isUTF8(JsVar *str);
JsVar *jswrap_espruino_toJS(JsVar *v);
JsVar *jswrap_espruino_memoryArea(int addr, int len);
void jswrap_espruino_setBootCode(JsVar *code, bool alwaysExec);
int jswrap_espruino_setClock(JsVar *options);
JsVar *jswrap_espruino_getClock();
void jswrap_espruino_setConsole(JsVar *device, JsVar *options);
JsVar *jswrap_espruino_getConsole();
int jswrap_espruino_reverseByte(int v);
void jswrap_espruino_dumpTimers();
void jswrap_espruino_dumpLockedVars();
void jswrap_espruino_dumpFreeList();
void jswrap_e_dumpFragmentation();
void jswrap_e_dumpVariables();
JsVar *jswrap_espruino_getSizeOf(JsVar *v, int depth);
JsVarInt jswrap_espruino_getAddressOf(JsVar *v, bool flatAddress);
void jswrap_espruino_mapInPlace(JsVar *from, JsVar *to, JsVar *map, JsVarInt bits);
JsVar *jswrap_espruino_lookupNoCase(JsVar *haystack, JsVar *needle, bool returnKey);
JsVar *jswrap_e_dumpStr();
JsVar *jswrap_espruino_CRC32(JsVar *data);
JsVar *jswrap_espruino_HSBtoRGB(JsVarFloat hue, JsVarFloat sat, JsVarFloat bri, int format);
void jswrap_espruino_setPassword(JsVar *pwd);
void jswrap_espruino_lockConsole();
void jswrap_espruino_setTimeZone(JsVarFloat zone);
void jswrap_espruino_setDST(JsVar *params);
JsVar *jswrap_espruino_memoryMap(JsVar *baseAddress, JsVar *registers);
void jswrap_espruino_asm(JsVar *callspec, JsVar *args);
void jswrap_espruino_compiledC(JsVar *code);
void jswrap_espruino_reboot();
void jswrap_espruino_rebootToDFU();
void jswrap_espruino_setUSBHID(JsVar *arr);
bool jswrap_espruino_sendUSBHID(JsVar *arr);
JsVarInt jswrap_espruino_getBattery();
void jswrap_espruino_setRTCPrescaler(int prescale);
int jswrap_espruino_getRTCPrescaler(bool calibrate);
JsVar *jswrap_espruino_getPowerUsage();
JsVar *jswrap_espruino_decodeUTF8(JsVar *str, JsVar *lookup, JsVar *replaceFn);
void jswrap_espruino_stopEventPropagation();
JsVar *jswrap_string_constructor(JsVar *a);
JsVar *jswrap_string_fromCharCode(JsVar *arr);
JsVar *jswrap_string_charAt_undefined(JsVar *parent, JsVarInt idx);
JsVar *jswrap_string_charAt(JsVar *parent, JsVarInt idx);
JsVar *jswrap_string_charCodeAt(JsVar *parent, JsVarInt idx);
int jswrap_string_indexOf(JsVar *parent, JsVar *substring, JsVar *fromIndex, bool lastIndexOf);
JsVar *jswrap_string_match(JsVar *parent, JsVar *subStr);
JsVar *jswrap_string_replace(JsVar *parent, JsVar *subStr, JsVar *newSubStr);
JsVar *jswrap_string_replaceAll(JsVar *parent, JsVar *subStr, JsVar *newSubStr);
JsVar *jswrap_string_substring(JsVar *parent, JsVarInt pStart, JsVar *vEnd);
JsVar *jswrap_string_substr(JsVar *parent, JsVarInt pStart, JsVar *vLen);
JsVar *jswrap_string_slice(JsVar *parent, JsVarInt pStart, JsVar *vEnd);
JsVar *jswrap_string_split(JsVar *parent, JsVar *split);
JsVar *jswrap_string_toUpperLowerCase(JsVar *parent, bool upper);
JsVar *jswrap_string_removeAccents(JsVar *parent);
JsVar *jswrap_string_trim(JsVar *parent);
JsVar *jswrap_string_concat(JsVar *parent, JsVar *args);
bool jswrap_string_startsWith(JsVar *parent, JsVar *search, int position);
bool jswrap_string_endsWith(JsVar *parent, JsVar *search, JsVar *length);
JsVar *jswrap_string_repeat(JsVar *parent, int count);
JsVar *jswrap_string_padX(JsVar *str, int targetLength, JsVar *padString, bool padStart);

JsVar *jswrap_regexp_constructor(JsVar *str, JsVar *flags);
JsVar *jswrap_regexp_exec(JsVar *parent, JsVar *str);
bool jswrap_regexp_test(JsVar *parent, JsVar *str);
bool jswrap_regexp_hasFlag(JsVar *parent, char flag);
JsExecInfo execInfo;
JsVar *jspeAssignmentExpression();
JsVar *jspeExpression();
JsVar *jspeUnaryExpression();
void jspeBlock();
void jspeBlockNoBrackets();
JsVar *jspeStatement();
JsVar *jspeFactor();
void jspEnsureIsPrototype(JsVar *instanceOf, JsVar *prototypeName);
JsVar *jspeArrowFunction(JsVar *funcVar, JsVar *a);
              void jspDebuggerLoopIfCtrlC() {
}
bool jspIsInterrupted() {
  return (execInfo.execute & EXEC_INTERRUPTED)!=0;
}
void jspSetInterrupted(bool interrupt) {
  if (interrupt)
    execInfo.execute = execInfo.execute | EXEC_INTERRUPTED;
  else
    execInfo.execute = execInfo.execute & (JsExecFlags)~EXEC_INTERRUPTED;
}
void jspSetError() {
  execInfo.execute = (execInfo.execute & (JsExecFlags)~EXEC_YES) | EXEC_ERROR;
}
bool jspHasError() {
  return (((execInfo.execute)&EXEC_ERROR_MASK)!=0);
}
void jspeiClearScopes() {
  jsvUnLock(execInfo.scopesVar);
  execInfo.scopesVar = 0;
}
bool jspeiAddScope(JsVar *scope) {
  if (!execInfo.scopesVar)
    execInfo.scopesVar = jsvNewEmptyArray();
  if (!execInfo.scopesVar) return false;
  jsvArrayPush(execInfo.scopesVar, scope);
  return true;
}
void jspeiRemoveScope() {
  if (!execInfo.scopesVar || !jsvGetArrayLength(execInfo.scopesVar)) {
    do { if (!(0)) jsAssertFail("bin/espruino_embedded.c",8854,""); } while(0);
    return;
  }
  jsvUnLock(jsvArrayPop(execInfo.scopesVar));
  if (!jsvGetFirstChild(execInfo.scopesVar)) {
    jsvUnLock(execInfo.scopesVar);
    execInfo.scopesVar = 0;
  }
}
JsVar *jspeiFindInScopes(const char *name) {
  if (execInfo.scopesVar) {
    JsVar *it = jsvLockSafe(jsvGetLastChild(execInfo.scopesVar));
    while (it) {
      JsVar *scope = jsvSkipName(it);
      JsVarRef next = jsvGetPrevSibling(it);
      JsVar *ref = jsvFindChildFromString(scope, name);
      jsvUnLock2(it, scope);
      if (ref) return ref;
      it = jsvLockSafe(next);
    }
  }
  return jsvFindChildFromString(execInfo.root, name);
}
JsVar *jspeiGetTopScope() {
  if (execInfo.scopesVar) {
    JsVar *scope = jsvGetLastArrayItem(execInfo.scopesVar);
    if (scope) return scope;
  }
  return jsvLockAgain(execInfo.root);
}
JsVar *jspFindPrototypeFor(const char *className) {
  JsVar *obj = jsvObjectGetChildIfExists(execInfo.root, className);
  if (!obj) return 0;
  JsVar *proto = jsvObjectGetChildIfExists(obj, "prototype");
  jsvUnLock(obj);
  return proto;
}
JsVar *jspeiFindChildFromStringInParents(JsVar *parent, const char *name) {
  if (jsvIsObject(parent)) {
    JsVar *inheritsFrom = jsvObjectGetChildIfExists(parent, "__proto__");
    if (!inheritsFrom)
      inheritsFrom = jspFindPrototypeFor("Object");
    if (inheritsFrom && inheritsFrom!=parent) {
      JsVar *child = jsvFindChildFromString(inheritsFrom, name);
      if (!child)
        child = jspeiFindChildFromStringInParents(inheritsFrom, name);
      jsvUnLock(inheritsFrom);
      if (child) return child;
    } else
      jsvUnLock(inheritsFrom);
  } else {
    const char *objectName = jswGetBasicObjectName(parent);
    while (objectName) {
      JsVar *objName = jsvFindChildFromString(execInfo.root, objectName);
      if (!objName) {
        objName = jspNewPrototype(objectName, true );
      }
      if (objName) {
        JsVar *result = 0;
        JsVar *obj = jsvSkipNameAndUnLock(objName);
        if (jsvHasChildren(obj)) {
          JsVar *proto = jspGetNamedField(obj, "prototype", false);
          if (proto) {
            result = jsvFindChildFromString(proto, name);
            jsvUnLock(proto);
          }
        }
        jsvUnLock(obj);
        if (result) return result;
      }
      objectName = jswGetBasicObjectPrototypeName(objectName);
    }
  }
  return 0;
}
JsVar *jspeiGetScopesAsVar() {
  if (!execInfo.scopesVar) return 0;
  if (jsvGetArrayLength(execInfo.scopesVar)==1) {
    JsVar *v = jsvGetLastArrayItem(execInfo.scopesVar);
    return v;
  }
  return jsvCopy(execInfo.scopesVar, true);
}
void jspeiLoadScopesFromVar(JsVar *arr) {
  jsvUnLock(execInfo.scopesVar);
  execInfo.scopesVar = 0;
  if (arr) {
    if (jsvIsArray(arr)) {
      execInfo.scopesVar = jsvCopy(arr, true);
    } else {
      execInfo.scopesVar = jsvNewArray(&arr, 1);
    }
  }
}
bool jspCheckStackPosition() {
  if (jsuGetFreeStack() < 512) {
    jsExceptionHere(JSET_ERROR, "Too much recursion - the stack is about to overflow");
    jspSetInterrupted(true);
    return false;
  }
  return true;
}
void jspSetNoExecute() {
  execInfo.execute = (execInfo.execute & (JsExecFlags)(int)~EXEC_RUN_MASK) | EXEC_NO;
}
void jspAppendStackTrace(JsVar *stackTrace, JsLex *lex) {
  JsvStringIterator it;
  jsvStringIteratorNew(&it, stackTrace, 0);
  jsvStringIteratorGotoEnd(&it);
  jslPrintStackTrace(jsvStringIteratorPrintfCallback, &it, lex);
  jsvStringIteratorFree(&it);
}
void jspSetException(JsVar *value) {
  JsVar *exception = jsvFindOrAddChildFromString(execInfo.hiddenRoot, "except");
  if (exception) {
    jsvSetValueOfName(exception, value);
    jsvUnLock(exception);
  }
  execInfo.execute = execInfo.execute | EXEC_EXCEPTION;
}
JsVar *jspGetException() {
  JsVar *exceptionName = jsvFindChildFromString(execInfo.hiddenRoot, "except");
  if (exceptionName) {
    JsVar *exception = jsvSkipName(exceptionName);
    jsvRemoveChildAndUnLock(execInfo.hiddenRoot, exceptionName);
    return exception;
  }
  return 0;
}
JsVar *jspGetStackTrace() {
  JsVar *stackTraceName = jsvFindChildFromString(execInfo.hiddenRoot, "sTrace");
  if (stackTraceName) {
    JsVar *stackTrace = jsvSkipName(stackTraceName);
    jsvRemoveChildAndUnLock(execInfo.hiddenRoot, stackTraceName);
    return stackTrace;
  }
  return 0;
}
__attribute__ ((noinline)) bool jspeFunctionArguments(JsVar *funcVar) {
  { if (!jslMatch(('('))) { ; return 0; } };
  while (lex->tk!=')') {
    if (funcVar) {
      char buf[64 +1];
      buf[0] = '\xFF';
      strcpy(&buf[1], jslGetTokenValueAsString());
      JsVar *param = jsvAddNamedChild(funcVar, 0, buf);
      if (!param) {
        jspSetError();
        return false;
      }
      param = jsvMakeFunctionParameter(param);
      jsvUnLock(param);
    }
    { if (!jslMatch((LEX_ID))) { ; return 0; } };
    if (lex->tk!=')') { if (!jslMatch((','))) { ; return 0; } };
  }
  { if (!jslMatch((')'))) { ; return 0; } };
  return true;
}
__attribute__ ((noinline)) bool jspeFunctionDefinitionInternal(JsVar *funcVar, bool expressionOnly) {
  JslCharPos funcBegin;
  bool forcePretokenise = false;
  if (expressionOnly) {
    if (funcVar)
      funcVar->flags = (funcVar->flags & ~JSV_VARTYPEMASK) | JSV_FUNCTION_RETURN;
  } else {
    JsExecFlags oldExec = execInfo.execute;
    execInfo.execute = EXEC_YES;
    { if (!jslMatch(('{'))) { ; return 0; } };
    execInfo.execute = oldExec;
    if (lex->tk==LEX_STR) {
      JsVar *tokenValue = jslGetTokenValueAsVar();
      if (jsvIsStringEqual(tokenValue, "compiled")) {
        jsWarn("Function marked with \"compiled\" uploaded in source form");
      }
      else if (jsvIsStringEqual(tokenValue, "ram")) {
        { do { if (!(0+lex->tk==(LEX_STR))) jsAssertFail("bin/espruino_embedded.c",9078,""); } while(0);jslGetNextToken(); };
        if (lex->tk==';') { do { if (!(0+lex->tk==(';'))) jsAssertFail("bin/espruino_embedded.c",9079,""); } while(0);jslGetNextToken(); };
        forcePretokenise = true;
      }
      jsvUnLock(tokenValue);
    }
    if (funcVar && lex->tk==LEX_R_RETURN) {
      funcVar->flags = (funcVar->flags & ~JSV_VARTYPEMASK) | JSV_FUNCTION_RETURN;
      { do { if (!(0+lex->tk==(LEX_R_RETURN))) jsAssertFail("bin/espruino_embedded.c",9124,""); } while(0);jslGetNextToken(); };
    }
  }
  jslSkipWhiteSpace();
  jslCharPosNew(&funcBegin, lex->sourceVar, lex->tokenStart);
  int lastTokenEnd = -1;
  lex->hadThisKeyword = lex->tk == LEX_R_THIS;
  if (!expressionOnly) {
    int brackets = 0;
    JsExecFlags oldExec = execInfo.execute;
    execInfo.execute = EXEC_NO;
    while (lex->tk && (brackets || lex->tk != '}')) {
      if (lex->tk == '{') brackets++;
      if (lex->tk == '}') brackets--;
      lastTokenEnd = (int)jsvStringIteratorGetIndex(&lex->it)-1;
      { do { if (!(0+lex->tk==(lex->tk))) jsAssertFail("bin/espruino_embedded.c",9140,""); } while(0);jslGetNextToken(); };
    }
    execInfo.execute = oldExec;
  } else {
    JsExecFlags oldExec = execInfo.execute;
    execInfo.execute = EXEC_NO;
    jsvUnLock(jspeAssignmentExpression());
    execInfo.execute = oldExec;
    lastTokenEnd = (int)lex->tokenStart;
  }
  bool hadThisKeyword = lex->hadThisKeyword;
  if (funcVar && lastTokenEnd>0) {
    JsVar *funcCodeVar;
    if (!forcePretokenise && jsvIsNativeString(lex->sourceVar)) {
      int s = (int)jsvStringIteratorGetIndex(&funcBegin.it) - 1;
      funcCodeVar = jsvNewNativeString(lex->sourceVar->varData.nativeStr.ptr + s, (unsigned int)(lastTokenEnd - s));
    } else {
      if (jsfGetFlag(JSF_PRETOKENISE) || forcePretokenise)
        funcCodeVar = jslNewTokenisedStringFromLexer(&funcBegin, (size_t)lastTokenEnd);
      else
        funcCodeVar = jslNewStringFromLexer(&funcBegin, (size_t)lastTokenEnd);
    }
    jsvAddNamedChildAndUnLock(funcVar, funcCodeVar, "\xFF""cod");
    JsVar *funcScopeVar = jspeiGetScopesAsVar();
    if (funcScopeVar) {
      jsvAddNamedChildAndUnLock(funcVar, funcScopeVar, "\xFF""sco");
    }
  }
  jslCharPosFree(&funcBegin);
  if (!expressionOnly) { if (!jslMatch(('}'))) { ; return 0; } };
  return hadThisKeyword;
}
__attribute__ ((noinline)) JsVar *jspeFunctionDefinition(bool parseNamedFunction) {
  JsVar *funcVar = 0;
  bool actuallyCreateFunction = (((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES);
  if (actuallyCreateFunction)
    funcVar = jsvNewWithFlags(JSV_FUNCTION);
  JsVar *functionInternalName = 0;
  if (parseNamedFunction && lex->tk==LEX_ID) {
    if (funcVar) functionInternalName = jslGetTokenValueAsVar();
    { do { if (!(0+lex->tk==(LEX_ID))) jsAssertFail("bin/espruino_embedded.c",9204,""); } while(0);jslGetNextToken(); };
  }
  if (!jspeFunctionArguments(funcVar)) {
    jsvUnLock2(functionInternalName, funcVar);
    return 0;
  }
  jspeFunctionDefinitionInternal(funcVar, false);
  if (funcVar && functionInternalName)
    jsvObjectSetChildAndUnLock(funcVar, "\xFF""nam", functionInternalName);
  return funcVar;
}
__attribute__ ((noinline)) bool jspeParseFunctionCallBrackets() {
  do { if (!(!(((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES))) jsAssertFail("bin/espruino_embedded.c",9227,""); } while(0);
  { if (!jslMatch(('('))) { ; return 0; } };
  while (!(((execInfo.execute)&EXEC_NO_PARSE_MASK)!=0) && lex->tk != ')') {
    jsvUnLock(jspeAssignmentExpression());
    if (lex->tk==LEX_ARROW_FUNCTION) {
      jsvUnLock(jspeArrowFunction(0, 0));
    }
    if (lex->tk!=')') { if (!jslMatch((','))) { ; return 0; } };
  }
  if (!(((execInfo.execute)&EXEC_NO_PARSE_MASK)!=0)) { if (!jslMatch((')'))) { ; return 0; } };
  return 0;
}
__attribute__ ((noinline)) JsVar *jspeFunctionCall(JsVar *function, JsVar *functionName, JsVar *thisArg, bool isParsing, int argCount, JsVar **argPtr) {
  if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES) && !function) {
    if (functionName)
      jsExceptionHere(JSET_ERROR, "Function %q not found!", functionName);
    else
      jsExceptionHere(JSET_ERROR, "Function not found!", functionName);
    return 0;
  }
  if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) if (!jspCheckStackPosition()) return 0;
  if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES) && function) {
    JsVar *returnVar = 0;
    if (!jsvIsFunction(function)) {
      jsExceptionHere(JSET_ERROR, "Expecting function, got %t", function);
      return 0;
    }
    JsVar *thisVar = jsvLockAgainSafe(thisArg);
    if (isParsing) { if (!jslMatch(('('))) { ; return 0; } };
    if (jsvIsNativeFunction(function)) {
      unsigned int argPtrSize = 0;
      int boundArgs = 0;
      JsvObjectIterator it;
      jsvObjectIteratorNew(&it, function);
      JsVar *param = jsvObjectIteratorGetKey(&it);
      while (jsvIsFunctionParameter(param)) {
        if ((unsigned)argCount>=argPtrSize) {
          unsigned int newArgPtrSize = (argPtrSize?argPtrSize:(unsigned int)argCount)*4;
          size_t newArgPtrByteSize = sizeof(JsVar*)*newArgPtrSize;
          if (jsuGetFreeStack() < 256+newArgPtrByteSize) {
            jsExceptionHere(JSET_ERROR, "Insufficient stack for this many arguments");
            jsvUnLock(thisVar);
            return 0;
          }
          JsVar **newArgPtr = (JsVar**)__builtin_alloca(newArgPtrByteSize);
          memcpy(newArgPtr, argPtr, (unsigned)argCount*sizeof(JsVar*));
          argPtr = newArgPtr;
          argPtrSize = newArgPtrSize;
        }
        int i;
        for (i=argCount-1;i>=boundArgs;i--)
          argPtr[i+1] = argPtr[i];
        argPtr[boundArgs] = jsvSkipName(param);
        argCount++;
        boundArgs++;
        jsvUnLock(param);
        jsvObjectIteratorNext(&it);
        param = jsvObjectIteratorGetKey(&it);
      }
      while (param) {
        if (jsvIsStringEqual(param, "\xFF""ths")) {
          jsvUnLock(thisVar);
          thisVar = jsvSkipName(param);
          break;
        }
        jsvUnLock(param);
        jsvObjectIteratorNext(&it);
        param = jsvObjectIteratorGetKey(&it);
      }
      jsvUnLock(param);
      jsvObjectIteratorFree(&it);
      int allocatedArgCount = boundArgs;
      if (isParsing) {
        while (!(((execInfo.execute)&EXEC_ERROR_MASK)!=0) && lex->tk!=')' && lex->tk!=LEX_EOF) {
          if ((unsigned)argCount>=argPtrSize) {
            unsigned int newArgPtrSize = argPtrSize?argPtrSize*4:16;
            JsVar **newArgPtr = (JsVar**)__builtin_alloca(sizeof(JsVar*)*newArgPtrSize);
            memcpy(newArgPtr, argPtr, (unsigned)argCount*sizeof(JsVar*));
            argPtr = newArgPtr;
            argPtrSize = newArgPtrSize;
          }
          argPtr[argCount++] = jsvSkipNameAndUnLock(jspeAssignmentExpression());
          if (lex->tk!=')') { if (!jslMatch((','))) { jsvUnLockMany((unsigned)argCount, argPtr);jsvUnLock(thisVar);; return 0; } };
        }
        { if (!jslMatch((')'))) { jsvUnLockMany((unsigned)argCount, argPtr);jsvUnLock(thisVar);; return 0; } };
        allocatedArgCount = argCount;
      }
      void *nativePtr = jsvGetNativeFunctionPtr(function);
      JsVar *oldThisVar = execInfo.thisVar;
      if (thisVar)
        execInfo.thisVar = jsvRef(thisVar);
      else {
        if (nativePtr==jswrap_eval) {
          if (execInfo.thisVar) execInfo.thisVar = jsvRef(execInfo.thisVar);
        } else {
          execInfo.thisVar = jsvRef(execInfo.root);
        }
      }
      if (nativePtr && !(((execInfo.execute)&EXEC_ERROR_MASK)!=0)) {
        returnVar = jsnCallFunction(nativePtr, function->varData.native.argTypes, thisVar, argPtr, argCount);
        do { if (!(!jsvIsName(returnVar))) jsAssertFail("bin/espruino_embedded.c",9375,""); } while(0);
      } else {
        returnVar = 0;
      }
      jsvUnLockMany((unsigned)allocatedArgCount, argPtr);
      if (execInfo.thisVar) jsvUnRef(execInfo.thisVar);
      execInfo.thisVar = oldThisVar;
    } else {
      JsVar *functionRoot = jsvNewWithFlags(JSV_FUNCTION);
      if (!functionRoot) {
        jspSetError();
        jsvUnLock(thisVar);
        return 0;
      }
      JsVar *functionScope = 0;
      JsVar *functionCode = 0;
      JsVar *functionInternalName = 0;
      JsvObjectIterator it;
      jsvObjectIteratorNew(&it, function);
      JsVar *param = jsvObjectIteratorGetKey(&it);
      JsVar *value = jsvObjectIteratorGetValue(&it);
      while (jsvIsFunctionParameter(param) && value) {
        jsvAddFunctionParameter(functionRoot, jsvNewFromStringVar(param,1,(0x7FFFFFFF)), value);
        jsvUnLock2(value, param);
        jsvObjectIteratorNext(&it);
        param = jsvObjectIteratorGetKey(&it);
        value = jsvObjectIteratorGetValue(&it);
      }
      jsvUnLock2(value, param);
      if (isParsing) {
        int hadParams = 0;
        while (!(((execInfo.execute)&EXEC_NO_PARSE_MASK)!=0) && lex->tk!=')') {
          JsVar *param = jsvObjectIteratorGetKey(&it);
          bool paramDefined = jsvIsFunctionParameter(param);
          if (lex->tk!=')' || paramDefined) {
            hadParams++;
            JsVar *value = 0;
            if (lex->tk!=')')
              value = jspeAssignmentExpression();
            value = jsvSkipNameAndUnLock(value);
            jsvAddFunctionParameter(functionRoot, paramDefined?jsvNewFromStringVar(param,1,(0x7FFFFFFF)):0, value);
            jsvUnLock(value);
            if (lex->tk!=')') { if (!jslMatch((','))) { ; return 0; } };
          }
          jsvUnLock(param);
          if (paramDefined) jsvObjectIteratorNext(&it);
        }
        { if (!jslMatch((')'))) { ; return 0; } };
      } else {
        int args = 0;
        while (args<argCount) {
          JsVar *param = jsvObjectIteratorGetKey(&it);
          bool paramDefined = jsvIsFunctionParameter(param);
          jsvAddFunctionParameter(functionRoot, paramDefined?jsvNewFromStringVar(param,1,(0x7FFFFFFF)):0, argPtr[args]);
          args++;
          jsvUnLock(param);
          if (paramDefined) jsvObjectIteratorNext(&it);
        }
      }
      while (jsvObjectIteratorHasValue(&it)) {
        JsVar *param = jsvObjectIteratorGetKey(&it);
        if (jsvIsString(param)) {
          if (jsvIsStringEqual(param, "\xFF""sco")) functionScope = jsvSkipName(param);
          else if (jsvIsStringEqual(param, "\xFF""cod")) functionCode = jsvSkipName(param);
          else if (jsvIsStringEqual(param, "\xFF""nam")) functionInternalName = jsvSkipName(param);
          else if (jsvIsStringEqual(param, "\xFF""ths")) {
            jsvUnLock(thisVar);
            thisVar = jsvSkipName(param);
          }
          else if (jsvIsFunctionParameter(param)) {
            JsVar *defaultVal = jsvSkipName(param);
            jsvAddFunctionParameter(functionRoot, jsvNewFromStringVar(param,1,(0x7FFFFFFF)), defaultVal);
            jsvUnLock(defaultVal);
          }
        }
        jsvUnLock(param);
        jsvObjectIteratorNext(&it);
      }
      jsvObjectIteratorFree(&it);
      if (functionInternalName) {
        JsVar *name = jsvMakeIntoVariableName(jsvNewFromStringVarComplete(functionInternalName), function);
        jsvAddName(functionRoot, name);
        jsvUnLock2(name, functionInternalName);
      }
      if (!(((execInfo.execute)&EXEC_ERROR_MASK)!=0)) {
        JsVar *oldScopeVar = execInfo.scopesVar;
        execInfo.scopesVar = 0;
        if (functionScope) {
          jspeiLoadScopesFromVar(functionScope);
          jsvUnLock(functionScope);
        }
        if (jspeiAddScope(functionRoot)) {
          JsVar *oldBaseScope = execInfo.baseScope;
          uint8_t oldBlockCount = execInfo.blockCount;
          execInfo.baseScope = functionRoot;
          execInfo.blockCount = 0;
          JsVar *oldThisVar = execInfo.thisVar;
          if (thisVar)
            execInfo.thisVar = jsvRef(thisVar);
          else
            execInfo.thisVar = jsvRef(execInfo.root);
          if (functionCode) {
            JsLex newLex;
            JsLex *oldLex = jslSetLex(&newLex);
            jslInit(functionCode);
            newLex.functionName = functionName;
            newLex.lastLex = oldLex;
            jsvUnLock(functionCode);
            functionCode = 0;
            JsExecFlags oldExecute = execInfo.execute;
            execInfo.execute = EXEC_YES | (execInfo.execute&(EXEC_CTRL_C_MASK|EXEC_ERROR_MASK));
            if (jsvIsFunctionReturn(function)) {
              if (lex->tk != ';' && lex->tk != '}')
                returnVar = jsvSkipNameAndUnLock(jspeExpression());
            } else {
              JsVar *returnVarName = jsvAddNamedChild(functionRoot, 0, "\xFF""rtn");
              execInfo.blockCount--;
              jspeBlockNoBrackets();
              execInfo.blockCount++;
              returnVar = jsvSkipName(returnVarName);
              if (returnVarName) {
                jsvRemoveChildAndUnLock(functionRoot, returnVarName);
              }
            }
            JsExecFlags hasError = execInfo.execute&EXEC_ERROR_MASK;
            execInfo.execute = (execInfo.execute&(JsExecFlags)(~EXEC_SAVE_RESTORE_MASK)) | (oldExecute&EXEC_SAVE_RESTORE_MASK);;
            jslKill();
            jslSetLex(oldLex);
            if (hasError)
              execInfo.execute |= hasError;
          }
          if (execInfo.thisVar) jsvUnRef(execInfo.thisVar);
          execInfo.thisVar = oldThisVar;
          jspeiRemoveScope();
          execInfo.baseScope = oldBaseScope;
          execInfo.blockCount = oldBlockCount;
        }
        jsvUnLock(execInfo.scopesVar);
        execInfo.scopesVar = oldScopeVar;
      }
      jsvUnLock2(functionCode, functionRoot);
    }
    jsvUnLock(thisVar);
    if (lex) jsvStringIteratorUpdatePtr(&lex->it);
    return returnVar;
  } else if (isParsing) {
    if (jspCheckStackPosition())
      jspeParseFunctionCallBrackets();
    return 0;
  } else return 0;
}
JsVar *jspGetNamedVariable(const char *tokenName) {
  JsVar *a = (((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES) ? jspeiFindInScopes(tokenName) : 0;
  if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES) && !a) {
    a = jswFindBuiltInFunction(0, tokenName);
    if (a) {
      if (jswIsBuiltInObject(tokenName)) {
        JsVar *name = jsvAddNamedChild(execInfo.root, a, tokenName);
        jsvUnLock(a);
        a = name;
      }
    } else {
      a = jsvNewNameFromString(tokenName);
    }
  }
  return a;
}
static __attribute__ ((noinline)) JsVar *jspGetNamedFieldInParents(JsVar *object, const char* name, bool returnName) {
  JsVar * child = jspeiFindChildFromStringInParents(object, name);
  if (!child) {
    child = jswFindBuiltInFunction(object, name);
  }
  if (child && returnName) {
    if (jsvIsName(child)) {
      JsVar *t = jsvGetValueOfName(child);
      jsvUnLock(child);
      child = t;
    }
    JsVar *nameVar = jsvNewNameFromString(name);
    JsVar *newChild = jsvCreateNewChild(object, nameVar, child);
    jsvUnLock2(nameVar, child);
    child = newChild;
    if (child && jsvIsArray(object) && !strcmp(name,"length"))
      child->flags |= JSV_CONSTANT;
  }
  if (!child) {
    if (jsvIsFunction(object) && strcmp(name, "prototype")==0) {
      JsVar *proto = jsvNewObject();
      jsvObjectSetChild(proto, "constructor", object);
      child = jsvAddNamedChild(object, proto, "prototype");
      jspEnsureIsPrototype(object, child);
      jsvUnLock(proto);
    } else if (strcmp(name, "__proto__")==0) {
      const char *objName = jswGetBasicObjectName(object);
      if (objName) {
        JsVar *p = jsvSkipNameAndUnLock(jspNewPrototype(objName, false ));
        JsVar *i = jsvNewNameFromString("__proto__");
        if (p) child = jsvCreateNewChild(object, i, p);
        jsvUnLock2(p, i);
      }
    }
  }
  return child;
}
JsVar *jspGetNamedField(JsVar *object, const char* name, bool returnName) {
  JsVar *child = 0;
  if (jsvHasChildren(object))
    child = jsvFindChildFromString(object, name);
  if (!child) {
    bool isPrototypeVar = strcmp(name, "prototype")==0;
    if (!isPrototypeVar)
      child = jspGetNamedFieldInParents(object, name, returnName);
    if (!child && jsvIsFunction(object) && isPrototypeVar) {
      JsVar *value = jsvNewObject();
      child = jsvAddNamedChild(object, value, "prototype");
      jsvUnLock(value);
    }
  }
  if (returnName) return child;
  else return jsvSkipNameAndUnLock(child);
}
JsVar *jspGetVarNamedField(JsVar *object, JsVar *nameVar, bool returnName) {
  JsVar *child = 0;
  if (jsvHasChildren(object))
    child = jsvFindChildFromVar(object, nameVar, false);
  if (!child) {
    if (jsvIsArrayBuffer(object) && jsvIsInt(nameVar)) {
      child = jsvMakeIntoVariableName(jsvNewFromInteger(jsvGetInteger(nameVar)), object);
      if (child)
        child->flags = (child->flags & ~JSV_VARTYPEMASK) | JSV_ARRAYBUFFERNAME;
    } else if (jsvIsString(object) && jsvIsInt(nameVar)) {
      JsVarInt idx = jsvGetInteger(nameVar);
      if (idx>=0 && idx<(JsVarInt)jsvGetStringLength(object)) {
        return jswrap_string_charAt_undefined(object, idx);
      } else if (returnName)
        child = jsvCreateNewChild(object, nameVar, 0);
    } else {
      char name[64];
      jsvGetString(nameVar, name, 64);
      child = jspGetNamedFieldInParents(object, name, returnName);
      if (!child && jsvIsFunction(object) && jsvIsStringEqual(nameVar, "prototype")) {
        JsVar *value = jsvNewObject();
        child = jsvAddNamedChild(object, value, "prototype");
        jsvUnLock(value);
      }
    }
  }
  if (returnName) return child;
  else return jsvSkipNameAndUnLock(child);
}
__attribute__ ((noinline)) JsVar *jspeFactorMember(JsVar *a, JsVar **parentResult) {
  JsVar *parent = 0;
  while (lex->tk=='.' || lex->tk=='[') {
    if (lex->tk == '.') {
      { do { if (!(0+lex->tk==('.'))) jsAssertFail("bin/espruino_embedded.c",9818,""); } while(0);jslGetNextToken(); };
      if (jslIsIDOrReservedWord()) {
        if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
          const char *name = jslGetTokenValueAsString();
          JsVar *aVar = jsvSkipNameWithParent(a,true,parent);
          JsVar *child = 0;
          if (aVar)
            child = jspGetNamedField(aVar, name, true);
          if (!child) {
            if (!jsvIsNullish(aVar)) {
              JsVar *nameVar = jsvNewNameFromString(jslGetTokenValueAsString());
              child = jsvCreateNewChild(aVar, nameVar, 0);
              jsvUnLock(nameVar);
            } else {
              jsExceptionHere(JSET_ERROR, "Can't read property '%s' of %s", name, jsvIsUndefined(aVar) ? "undefined" : "null");
            }
          }
          jsvUnLock(parent);
          parent = aVar;
          jsvUnLock(a);
          a = child;
        }
        jslGetNextToken();
      } else {
        { if (!jslMatch((LEX_ID))) { ; return a; } };
      }
    } else if (lex->tk == '[') {
      JsVar *index;
      { do { if (!(0+lex->tk==('['))) jsAssertFail("bin/espruino_embedded.c",9853,""); } while(0);jslGetNextToken(); };
      if (!jspCheckStackPosition()) return parent;
      index = jsvSkipNameAndUnLock(jspeAssignmentExpression());
      { if (!jslMatch((']'))) { jsvUnLock2(parent, index);; return a; } };
      if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
        index = jsvAsArrayIndexAndUnLock(index);
        JsVar *aVar = jsvSkipNameWithParent(a,true,parent);
        JsVar *child = 0;
        if (aVar)
          child = jspGetVarNamedField(aVar, index, true);
        if (!child) {
          if (jsvHasChildren(aVar)) {
            child = jsvCreateNewChild(aVar, index, 0);
          } else {
            jsExceptionHere(JSET_ERROR, "Field or method %q does not already exist, and can't create it on %t", index, aVar);
          }
        }
        jsvUnLock(parent);
        parent = jsvLockAgainSafe(aVar);
        jsvUnLock(a);
        a = child;
        jsvUnLock(aVar);
      }
      jsvUnLock(index);
    } else {
      do { if (!(0)) jsAssertFail("bin/espruino_embedded.c",9881,""); } while(0);
    }
  }
  if (parentResult) *parentResult = parent;
  else jsvUnLock(parent);
  return a;
}
__attribute__ ((noinline)) JsVar *jspeConstruct(JsVar *func, JsVar *funcName, bool hasArgs) {
  do { if (!((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES))) jsAssertFail("bin/espruino_embedded.c",9891,""); } while(0);
  if (!jsvIsFunction(func)) {
    jsExceptionHere(JSET_ERROR, "Constructor should be a function, but is %t", func);
    return 0;
  }
  JsVar *thisObj = jsvNewObject();
  if (!thisObj) return 0;
  JsVar *prototypeName = jsvFindOrAddChildFromString(func, "prototype");
  jspEnsureIsPrototype(func, prototypeName);
  jsvAddNamedChildAndUnLock(thisObj, jsvSkipNameAndUnLock(prototypeName), "__proto__");
  JsVar *a = jspeFunctionCall(func, funcName, thisObj, hasArgs, 0, 0);
  if (a) {
    jsvUnLock(thisObj);
    thisObj = a;
  } else {
    jsvUnLock(a);
  }
  return thisObj;
}
__attribute__ ((noinline)) JsVar *jspeFactorFunctionCall() {
  bool isConstructor = false;
  if (lex->tk==LEX_R_NEW) {
    { do { if (!(0+lex->tk==(LEX_R_NEW))) jsAssertFail("bin/espruino_embedded.c",9927,""); } while(0);jslGetNextToken(); };
    isConstructor = true;
    if (lex->tk==LEX_R_NEW) {
      jsExceptionHere(JSET_ERROR, "Nesting 'new' operators is unsupported");
      jspSetError();
      return 0;
    }
  }
  JsVar *parent = 0;
  JsVar *a = 0;
  bool hasSetCurrentClassConstructor = false;
  if (lex->tk==LEX_R_SUPER) {
    { do { if (!(0+lex->tk==(LEX_R_SUPER))) jsAssertFail("bin/espruino_embedded.c",9943,""); } while(0);jslGetNextToken(); };
    if (jsvIsObject(execInfo.thisVar)) {
      JsVar *proto1;
      if (execInfo.currentClassConstructor && lex->tk=='(') {
        proto1 = jsvObjectGetChildIfExists(execInfo.currentClassConstructor, "prototype");
      } else {
        proto1 = jsvObjectGetChildIfExists(execInfo.thisVar, "__proto__");
      }
      JsVar *proto2 = jsvIsObject(proto1) ? jsvObjectGetChildIfExists(proto1, "__proto__") : 0;
      jsvUnLock(proto1);
      if (!proto2) {
        jsExceptionHere(JSET_SYNTAXERROR, "Calling 'super' outside of class");
        return 0;
      }
      if (lex->tk=='(') {
        JsVar *constr = jsvObjectGetChildIfExists(proto2, "constructor");
        jsvUnLock(proto2);
        execInfo.currentClassConstructor = constr;
        hasSetCurrentClassConstructor = true;
        a = constr;
      } else {
        a = proto2;
      }
    } else if (jsvIsFunction(execInfo.thisVar)) {
      JsVar *proto1 = jsvObjectGetChildIfExists(execInfo.thisVar, "prototype");
      JsVar *proto2 = jsvIsObject(proto1) ? jsvObjectGetChildIfExists(proto1, "__proto__") : 0;
      jsvUnLock(proto1);
      if (!proto2) {
        jsExceptionHere(JSET_SYNTAXERROR, "Calling 'super' outside of class");
        return 0;
      }
      JsVar *constr = jsvObjectGetChildIfExists(proto2, "constructor");
      jsvUnLock(proto2);
      a = constr;
    } else {
      jsExceptionHere(JSET_SYNTAXERROR, "Calling 'super' outside of class");
      return 0;
    }
    JsVar *superVar = a;
    a = jspeFactorMember(a, &parent);
    if (!parent || parent==superVar) {
      jsvUnLock(parent);
      parent = jsvLockAgain(execInfo.thisVar);
    }
  } else
  a = jspeFactorMember(jspeFactor(), &parent);
  if (lex->tk==LEX_TEMPLATE_LITERAL) {
    jsExceptionHere(JSET_SYNTAXERROR, "Tagged Templates are not supported");
    jsvUnLock2(a, parent);
    return 0;
  }
  while ((lex->tk=='(' || (isConstructor && (((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES))) && !jspIsInterrupted()) {
    JsVar *funcName = a;
    JsVar *func = jsvSkipName(funcName);
    if (isConstructor && (((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
      bool parseArgs = lex->tk=='(';
      a = jspeConstruct(func, funcName, parseArgs);
      isConstructor = false;
    } else
      a = jspeFunctionCall(func, funcName, parent, true, 0, 0);
    jsvUnLock3(funcName, func, parent);
    parent=0;
    a = jspeFactorMember(a, &parent);
  }
  if (hasSetCurrentClassConstructor)
    execInfo.currentClassConstructor = 0;
  if (parent && jsvIsBasicName(a) && !jsvIsNewChild(a)) {
    JsVar *value = jsvLockSafe(jsvGetFirstChild(a));
    if (jsvIsGetterOrSetter(value)) {
      JsVar *nameVar = jsvCopyNameOnly(a,false,true);
      JsVar *newChild = jsvCreateNewChild(parent, nameVar, value);
      jsvUnLock2(nameVar, a);
      a = newChild;
    }
    jsvUnLock(value);
  }
  jsvUnLock(parent);
  return a;
}
__attribute__ ((noinline)) JsVar *jspeFactorObject() {
  if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
    JsVar *contents = jsvNewObject();
    if (!contents) {
      jspSetError();
      return 0;
    }
    { if (!jslMatch(('{'))) { ; return contents; } };
    while (!(((execInfo.execute)&EXEC_NO_PARSE_MASK)!=0) && lex->tk != '}') {
      JsVar *varName = 0;
      bool isIdentifier = 0;
      if (jslIsIDOrReservedWord()) {
        isIdentifier = lex->tk == LEX_ID;
        if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES))
          varName = jslGetTokenValueAsVar();
        jslGetNextToken();
      } else if (
          lex->tk==LEX_STR ||
          lex->tk==LEX_FLOAT ||
          lex->tk==LEX_INT) {
        varName = jspeFactor();
      } else {
        { if (!jslMatch((LEX_ID))) { ; return contents; } };
      }
      if (lex->tk==LEX_ID && jsvIsString(varName)) {
        bool isGetter = jsvIsStringEqual(varName, "get");
        bool isSetter = jsvIsStringEqual(varName, "set");
        if (isGetter || isSetter) {
          jsvUnLock(varName);
          varName = jslGetTokenValueAsVar();
          { do { if (!(0+lex->tk==(LEX_ID))) jsAssertFail("bin/espruino_embedded.c",10102,""); } while(0);jslGetNextToken(); };
          JsVar *method = jspeFunctionDefinition(false);
          jsvAddGetterOrSetter(contents, varName, isGetter, method);
          jsvUnLock(method);
        }
      } else
      if (lex->tk == '(') {
        JsVar *contentsName = jsvFindChildFromVar(contents, varName, true);
        if (contentsName) {
          JsVar *method = jspeFunctionDefinition(false);
          jsvUnLock2(jsvSetValueOfName(contentsName, method), method);
        }
      } else
      if (isIdentifier && (lex->tk == ',' || lex->tk == '}') && jsvIsString(varName)) {
        if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
          varName = jsvAsArrayIndexAndUnLock(varName);
          JsVar *contentsName = jsvFindChildFromVar(contents, varName, true);
          if (contentsName) {
            char buf[64];
            jsvGetString(varName, buf, 64);
            JsVar *value = jsvSkipNameAndUnLock(jspGetNamedVariable(buf));
            jsvUnLock2(jsvSetValueOfName(contentsName, value), value);
          }
        }
      } else
      {
        { if (!jslMatch((':'))) { jsvUnLock(varName); return contents; } };
        if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
          varName = jsvAsArrayIndexAndUnLock(varName);
          JsVar *contentsName = jsvFindChildFromVar(contents, varName, true);
          if (contentsName) {
            JsVar *value = jsvSkipNameAndUnLock(jspeAssignmentExpression());
            jsvUnLock2(jsvSetValueOfName(contentsName, value), value);
          }
        }
      }
      jsvUnLock(varName);
      if (lex->tk != '}') { if (!jslMatch((','))) { ; return contents; } };
    }
    { if (!jslMatch(('}'))) { ; return contents; } };
    return contents;
  } else {
    jspeBlock();
    return 0;
  }
}
__attribute__ ((noinline)) JsVar *jspeFactorArray() {
  int idx = 0;
  JsVar *contents = 0;
  if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
    contents = jsvNewEmptyArray();
    if (!contents) {
      jspSetError();
      return 0;
    }
  }
  { if (!jslMatch(('['))) { ; return contents; } };
  while (!(((execInfo.execute)&EXEC_NO_PARSE_MASK)!=0) && lex->tk != ']') {
    if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
      if (lex->tk != ',') {
        JsVar *aVar = aVar = jsvSkipNameAndUnLock(jspeAssignmentExpression());
        JsVar *indexName = jsvMakeIntoVariableName(jsvNewFromInteger(idx), aVar);
        if (indexName) {
          jsvAddName(contents, indexName);
          jsvUnLock(indexName);
        }
        jsvUnLock(aVar);
      }
    } else {
      jsvUnLock(jspeAssignmentExpression());
    }
    if (lex->tk != ']') { if (!jslMatch((','))) { ; return contents; } };
    idx++;
  }
  if (contents) jsvSetArrayLength(contents, idx, false);
  { if (!jslMatch((']'))) { ; return contents; } };
  return contents;
}
__attribute__ ((noinline)) void jspEnsureIsPrototype(JsVar *instanceOf, JsVar *prototypeName) {
  if (!prototypeName) return;
  JsVar *prototypeVar = jsvSkipName(prototypeName);
  if (!(jsvIsObject(prototypeVar) || jsvIsFunction(prototypeVar))) {
    if (!jsvIsUndefined(prototypeVar))
      jsExceptionHere(JSET_TYPEERROR, "Prototype should be an object, got %t", prototypeVar);
    jsvUnLock(prototypeVar);
    prototypeVar = jsvNewObject();
    JsVar *lastName = jsvSkipToLastName(prototypeName);
    jsvSetValueOfName(lastName, prototypeVar);
    jsvUnLock(lastName);
  }
  JsVar *constructor = jsvFindOrAddChildFromString(prototypeVar, "constructor");
  if (constructor) jsvSetValueOfName(constructor, instanceOf);
  jsvUnLock2(constructor, prototypeVar);
}
__attribute__ ((noinline)) JsVar *jspeFactorTypeOf() {
  { do { if (!(0+lex->tk==(LEX_R_TYPEOF))) jsAssertFail("bin/espruino_embedded.c",10209,""); } while(0);jslGetNextToken(); };
  JsVar *a = jspeUnaryExpression();
  JsVar *result = 0;
  if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
    if (!jsvIsVariableDefined(a)) {
      result=jsvNewFromString("undefined");
    } else {
      a = jsvSkipNameAndUnLock(a);
      result=jsvNewFromString(jsvGetTypeOf(a));
    }
  }
  jsvUnLock(a);
  return result;
}
__attribute__ ((noinline)) JsVar *jspeFactorDelete() {
  { do { if (!(0+lex->tk==(LEX_R_DELETE))) jsAssertFail("bin/espruino_embedded.c",10226,""); } while(0);jslGetNextToken(); };
  JsVar *parent = 0;
  JsVar *a = jspeFactorMember(jspeFactor(), &parent);
  JsVar *result = 0;
  if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
    bool ok = false;
    if (jsvIsName(a) && !jsvIsNewChild(a)) {
      if (!parent && jsvIsChild(execInfo.root, a))
        parent = jsvLockAgain(execInfo.root);
      if (jsvHasChildren(parent) && jsvIsChild(parent, a)) {
        if (jsvIsArray(parent)) {
          JsVarInt l = jsvGetArrayLength(parent);
          jsvRemoveChild(parent, a);
          jsvSetArrayLength(parent, l, false);
        } else {
          jsvRemoveChild(parent, a);
        }
        ok = true;
      }
    }
    result = jsvNewFromBool(ok);
  }
  jsvUnLock2(a, parent);
  return result;
}
JsVar *jspeTemplateLiteral() {
  JsVar *a = 0;
  if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
    JsVar *template = jslGetTokenValueAsVar();
    a = jsvNewFromEmptyString();
    if (a && template) {
      JsvStringIterator it, dit;
      jsvStringIteratorNew(&it, template, 0);
      jsvStringIteratorNew(&dit, a, 0);
      while (jsvStringIteratorHasChar(&it)) {
        char ch = jsvStringIteratorGetCharAndNext(&it);
        if (ch=='$') {
          ch = jsvStringIteratorGetChar(&it);
          if (ch=='{') {
            jsvStringIteratorNext(&it);
            int brackets = 1;
            JsVar *expr = jsvNewFromEmptyString();
            if (!expr) break;
            JsvStringIterator eit;
            jsvStringIteratorNew(&eit, expr, 0);
            while (jsvStringIteratorHasChar(&it)) {
              ch = jsvStringIteratorGetCharAndNext(&it);
              if (ch=='{') brackets++;
              if (ch=='}') {
                brackets--;
                if (!brackets) break;
              }
              jsvStringIteratorAppend(&eit, ch);
            }
            jsvStringIteratorFree(&eit);
            JsVar *result = jspEvaluateExpressionVar(expr);
            jsvUnLock(expr);
            result = jsvAsStringAndUnLock(result);
            jsvStringIteratorAppendString(&dit, result, 0, (0x7FFFFFFF));
            jsvUnLock(result);
          } else {
            jsvStringIteratorAppend(&dit, '$');
          }
        } else {
          jsvStringIteratorAppend(&dit, ch);
        }
      }
      jsvStringIteratorFree(&it);
      jsvStringIteratorFree(&dit);
    }
    jsvUnLock(template);
  }
  { do { if (!(0+lex->tk==(LEX_TEMPLATE_LITERAL))) jsAssertFail("bin/espruino_embedded.c",10311,""); } while(0);jslGetNextToken(); };
  return a;
}
__attribute__ ((noinline)) JsVar *jspeAddNamedFunctionParameter(JsVar *funcVar, JsVar *name) {
  if (!funcVar) funcVar = jsvNewWithFlags(JSV_FUNCTION);
  if (name) {
    char buf[64 +1];
    buf[0] = '\xFF';
    jsvGetString(name, &buf[1], 64);
    JsVar *param = jsvAddNamedChild(funcVar, 0, buf);
    param = jsvMakeFunctionParameter(param);
    jsvUnLock(param);
  }
  return funcVar;
}
__attribute__ ((noinline)) JsVar *jspeArrowFunction(JsVar *funcVar, JsVar *a) {
  { do { if (!(0+lex->tk==(LEX_ARROW_FUNCTION))) jsAssertFail("bin/espruino_embedded.c",10333,""); } while(0);jslGetNextToken(); };
  if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
    do { if (!(!a || jsvIsName(a))) jsAssertFail("bin/espruino_embedded.c",10335,""); } while(0);
    funcVar = jspeAddNamedFunctionParameter(funcVar, a);
  }
  bool expressionOnly = lex->tk!='{';
  bool fnIncludesThis = jspeFunctionDefinitionInternal(funcVar, expressionOnly);
  if (fnIncludesThis)
    jsvObjectSetChild(funcVar, "\xFF""ths", execInfo.thisVar);
  return funcVar;
}
__attribute__ ((noinline)) JsVar *jspeExpressionOrArrowFunction() {
  JsVar *a = 0;
  JsVar *funcVar = 0;
  bool allNames = true;
  while (lex->tk!=')' && !(((execInfo.execute)&EXEC_NO_PARSE_MASK)!=0)) {
    if (allNames && a) {
      funcVar = jspeAddNamedFunctionParameter(funcVar, a);
    }
    jsvUnLock(a);
    a = jspeAssignmentExpression();
    if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES) && !(jsvIsName(a) && jsvIsString(a))) allNames = false;
    if (lex->tk!=')') { if (!jslMatch((','))) { jsvUnLock2(a,funcVar); return 0; } };
  }
  { if (!jslMatch((')'))) { jsvUnLock2(a,funcVar); return 0; } };
  if (allNames && lex->tk==LEX_ARROW_FUNCTION) {
    funcVar = jspeArrowFunction(funcVar, a);
    jsvUnLock(a);
    return funcVar;
  } else {
    jsvUnLock(funcVar);
    return a;
  }
}
__attribute__ ((noinline)) JsVar *jspeClassDefinition(bool parseNamedClass) {
  JsVar *classFunction = 0;
  JsVar *classPrototype = 0;
  JsVar *classStaticFields = 0;
  bool actuallyCreateClass = (((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES);
  if (actuallyCreateClass) {
    classFunction = jsvNewWithFlags(JSV_FUNCTION);
    classStaticFields = jsvNewObject();
    JsVar *scopeVar = jspeiGetScopesAsVar();
    if (scopeVar)
      jsvAddNamedChildAndUnLock(classFunction, scopeVar, "\xFF""sco");
  }
  if (parseNamedClass && lex->tk==LEX_ID) {
    if (classStaticFields)
      jsvObjectSetChildAndUnLock(classStaticFields, "\xFF""nam", jslGetTokenValueAsVar());
    { do { if (!(0+lex->tk==(LEX_ID))) jsAssertFail("bin/espruino_embedded.c",10404,""); } while(0);jslGetNextToken(); };
  }
  if (classFunction) {
    JsVar *prototypeName = jsvFindOrAddChildFromString(classFunction, "prototype");
    jspEnsureIsPrototype(classFunction, prototypeName);
    classPrototype = jsvSkipName(prototypeName);
    jsvUnLock(prototypeName);
  }
  if (lex->tk==LEX_R_EXTENDS) {
    { do { if (!(0+lex->tk==(LEX_R_EXTENDS))) jsAssertFail("bin/espruino_embedded.c",10413,""); } while(0);jslGetNextToken(); };
    JsVar *extendsFromName = 0;
    JsVar *extendsFrom = 0;
    if (actuallyCreateClass) {
      extendsFromName = jspGetNamedVariable(jslGetTokenValueAsString());
      extendsFrom = jsvSkipName(extendsFromName);
    }
    { if (!jslMatch((LEX_ID))) { jsvUnLock4(extendsFrom,classFunction,classStaticFields,classPrototype); return 0; } };
    if (classPrototype) {
      if (jsvIsFunction(extendsFrom)) {
        JsVar *extendsFromProto = jsvObjectGetChildIfExists(extendsFrom, "prototype");
        if (extendsFromProto) {
          jsvObjectSetChild(classPrototype, "__proto__", extendsFromProto);
          jsvObjectSetChildAndUnLock(classFunction, "\xFF""cod",
              jsvVarPrintf("%v.apply(this,arguments)", extendsFromName));
          jsvUnLock(extendsFromProto);
        }
      } else
        jsExceptionHere(JSET_SYNTAXERROR, "'extends' argument %q should be a function, got %t", extendsFromName, extendsFrom);
    }
    jsvUnLock2(extendsFrom, extendsFromName);
  }
  { if (!jslMatch(('{'))) { jsvUnLock3(classFunction,classStaticFields,classPrototype); return 0; } };
  while ((lex->tk==LEX_ID || lex->tk==LEX_R_STATIC) && !jspIsInterrupted()) {
    bool isStatic = lex->tk==LEX_R_STATIC;
    if (isStatic) { do { if (!(0+lex->tk==(LEX_R_STATIC))) jsAssertFail("bin/espruino_embedded.c",10442,""); } while(0);jslGetNextToken(); };
    JsVar *funcName = jslGetTokenValueAsVar();
    bool isConstructor = jsvIsStringEqual(funcName, "constructor");
    { if (!jslMatch((LEX_ID))) { jsvUnLock4(funcName,classFunction,classStaticFields,classPrototype); return 0; } };
    bool isGetter = false, isSetter = false;
    if (lex->tk==LEX_ID) {
      isGetter = jsvIsStringEqual(funcName, "get");
      isSetter = jsvIsStringEqual(funcName, "set");
      if (isGetter || isSetter) {
        jsvUnLock(funcName);
        funcName = jslGetTokenValueAsVar();
        { do { if (!(0+lex->tk==(LEX_ID))) jsAssertFail("bin/espruino_embedded.c",10455,""); } while(0);jslGetNextToken(); };
      }
    }
    JsVar *obj = isStatic ? classStaticFields : classPrototype;
    if (obj) {
      if (isGetter || isSetter || isConstructor || lex->tk=='(') {
        JsVar *method = jspeFunctionDefinition(false);
        if (isConstructor) {
          jswrap_function_replaceWith(classFunction, method);
        } else if (isGetter || isSetter) {
          jsvAddGetterOrSetter(obj, funcName, isGetter, method);
        } else {
          jsvObjectSetChildVar(obj, funcName, method);
        }
        jsvUnLock(method);
      } else {
        { if (!jslMatch(('='))) { jsvUnLock4(funcName,classFunction,classStaticFields,classPrototype); return 0; } };
        JsVar *value = jsvSkipNameAndUnLock(jspeAssignmentExpression());
        jsvObjectSetChildVar(obj, funcName, value);
        jsvUnLock(value);
      }
    }
    while (lex->tk==';') { do { if (!(0+lex->tk==(';'))) jsAssertFail("bin/espruino_embedded.c",10480,""); } while(0);jslGetNextToken(); };
    jsvUnLock(funcName);
  }
  jsvUnLock(classPrototype);
  jsvObjectAppendAll(classFunction, classStaticFields);
  jsvUnLock(classStaticFields);
  { if (!jslMatch(('}'))) { jsvUnLock(classFunction); return 0; } };
  return classFunction;
}
__attribute__ ((noinline)) JsVar *jspeFactor() {
  if (lex->tk==LEX_ID) {
    JsVar *a = jspGetNamedVariable(jslGetTokenValueAsString());
    { do { if (!(0+lex->tk==(LEX_ID))) jsAssertFail("bin/espruino_embedded.c",10498,""); } while(0);jslGetNextToken(); };
    if (lex->tk==LEX_TEMPLATE_LITERAL)
      jsExceptionHere(JSET_SYNTAXERROR, "Tagged template literals not supported");
    else if (lex->tk==LEX_ARROW_FUNCTION &&
             (jsvIsName(a) || (a==0 && !(((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)))) {
      JsVar *funcVar = jspeArrowFunction(0,a);
      jsvUnLock(a);
      a=funcVar;
    }
    return a;
  } else if (lex->tk==LEX_INT) {
    JsVar *v = 0;
    if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
      v = jslGetTokenValueAsVar();
    }
    { do { if (!(0+lex->tk==(LEX_INT))) jsAssertFail("bin/espruino_embedded.c",10516,""); } while(0);jslGetNextToken(); };
    return v;
  } else if (lex->tk==LEX_FLOAT) {
    JsVar *v = 0;
    if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
      v = jsvNewFromFloat(stringToFloat(jslGetTokenValueAsString()));
    }
    { do { if (!(0+lex->tk==(LEX_FLOAT))) jsAssertFail("bin/espruino_embedded.c",10523,""); } while(0);jslGetNextToken(); };
    return v;
  } else if (lex->tk=='(') {
    { do { if (!(0+lex->tk==('('))) jsAssertFail("bin/espruino_embedded.c",10526,""); } while(0);jslGetNextToken(); };
    if (!jspCheckStackPosition()) return 0;
    return jspeExpressionOrArrowFunction();
  } else if (lex->tk==LEX_R_TRUE) {
    { do { if (!(0+lex->tk==(LEX_R_TRUE))) jsAssertFail("bin/espruino_embedded.c",10538,""); } while(0);jslGetNextToken(); };
    return (((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES) ? jsvNewFromBool(true) : 0;
  } else if (lex->tk==LEX_R_FALSE) {
    { do { if (!(0+lex->tk==(LEX_R_FALSE))) jsAssertFail("bin/espruino_embedded.c",10541,""); } while(0);jslGetNextToken(); };
    return (((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES) ? jsvNewFromBool(false) : 0;
  } else if (lex->tk==LEX_R_NULL) {
    { do { if (!(0+lex->tk==(LEX_R_NULL))) jsAssertFail("bin/espruino_embedded.c",10544,""); } while(0);jslGetNextToken(); };
    return (((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES) ? jsvNewWithFlags(JSV_NULL) : 0;
  } else if (lex->tk==LEX_R_UNDEFINED) {
    { do { if (!(0+lex->tk==(LEX_R_UNDEFINED))) jsAssertFail("bin/espruino_embedded.c",10547,""); } while(0);jslGetNextToken(); };
    return 0;
  } else if (lex->tk==LEX_STR) {
    JsVar *a = 0;
    if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
      a = jslGetTokenValueAsVar();
    }
    { do { if (!(0+lex->tk==(LEX_STR))) jsAssertFail("bin/espruino_embedded.c",10558,""); } while(0);jslGetNextToken(); };
    return a;
  } else if (lex->tk==LEX_TEMPLATE_LITERAL) {
    return jspeTemplateLiteral();
  } else if (lex->tk==LEX_REGEX) {
    JsVar *a = 0;
    if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
      JsVar *regex = jslGetTokenValueAsVar();
      size_t regexEnd = 0, regexLen = 0;
      JsvStringIterator it;
      jsvStringIteratorNew(&it, regex, 0);
      while (jsvStringIteratorHasChar(&it)) {
        regexLen++;
        if (jsvStringIteratorGetCharAndNext(&it)=='/')
          regexEnd = regexLen;
      }
      jsvStringIteratorFree(&it);
      JsVar *flags = 0;
      if (regexEnd < regexLen)
        flags = jsvNewFromStringVar(regex, regexEnd, (0x7FFFFFFF));
      JsVar *regexSource = jsvNewFromStringVar(regex, 1, regexEnd-2);
      a = jswrap_regexp_constructor(regexSource, flags);
      jsvUnLock3(regex, flags, regexSource);
    }
    { do { if (!(0+lex->tk==(LEX_REGEX))) jsAssertFail("bin/espruino_embedded.c",10588,""); } while(0);jslGetNextToken(); };
    return a;
  } else if (lex->tk=='{') {
    if (!jspCheckStackPosition()) return 0;
    return jspeFactorObject();
  } else if (lex->tk=='[') {
    if (!jspCheckStackPosition()) return 0;
    return jspeFactorArray();
  } else if (lex->tk==LEX_R_FUNCTION) {
    if (!jspCheckStackPosition()) return 0;
    { do { if (!(0+lex->tk==(LEX_R_FUNCTION))) jsAssertFail("bin/espruino_embedded.c",10598,""); } while(0);jslGetNextToken(); };
    return jspeFunctionDefinition(true);
  } else if (lex->tk==LEX_R_CLASS) {
    if (!jspCheckStackPosition()) return 0;
    { do { if (!(0+lex->tk==(LEX_R_CLASS))) jsAssertFail("bin/espruino_embedded.c",10603,""); } while(0);jslGetNextToken(); };
    return jspeClassDefinition(true);
  } else if (lex->tk==LEX_R_THIS) {
    { do { if (!(0+lex->tk==(LEX_R_THIS))) jsAssertFail("bin/espruino_embedded.c",10608,""); } while(0);jslGetNextToken(); };
    return jsvLockAgain( execInfo.thisVar ? execInfo.thisVar : execInfo.root );
  } else if (lex->tk==LEX_R_DELETE) {
    if (!jspCheckStackPosition()) return 0;
    return jspeFactorDelete();
  } else if (lex->tk==LEX_R_TYPEOF) {
    if (!jspCheckStackPosition()) return 0;
    return jspeFactorTypeOf();
  } else if (lex->tk==LEX_R_VOID) {
    if (!jspCheckStackPosition()) return 0;
    { do { if (!(0+lex->tk==(LEX_R_VOID))) jsAssertFail("bin/espruino_embedded.c",10618,""); } while(0);jslGetNextToken(); };
    jsvUnLock(jspeUnaryExpression());
    return 0;
  }
  { if (!jslMatch((LEX_EOF))) { ; return 0; } };
  jsExceptionHere(JSET_SYNTAXERROR, "Unexpected end of Input");
  return 0;
}
__attribute__ ((noinline)) JsVar *__jspePostfixExpression(JsVar *a) {
  while (lex->tk==LEX_PLUSPLUS || lex->tk==LEX_MINUSMINUS) {
    int op = lex->tk;
    { do { if (!(0+lex->tk==(op))) jsAssertFail("bin/espruino_embedded.c",10630,""); } while(0);jslGetNextToken(); };
    if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
      JsVar *one = jsvNewFromInteger(1);
      JsVar *oldValue = jsvAsNumberAndUnLock(jsvSkipName(a));
      JsVar *res = jsvMathsOpSkipNames(oldValue, one, op==LEX_PLUSPLUS ? '+' : '-');
      jsvUnLock(one);
      jsvReplaceWith(a, res);
      jsvUnLock(res);
      jsvUnLock(a);
      a = oldValue;
    }
  }
  return a;
}
__attribute__ ((noinline)) JsVar *jspePostfixExpression() {
  JsVar *a;
  if (lex->tk==LEX_PLUSPLUS || lex->tk==LEX_MINUSMINUS) {
    int op = lex->tk;
    { do { if (!(0+lex->tk==(op))) jsAssertFail("bin/espruino_embedded.c",10653,""); } while(0);jslGetNextToken(); };
    a = jspePostfixExpression();
    if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
      JsVar *one = jsvNewFromInteger(1);
      JsVar *res = jsvMathsOpSkipNames(a, one, op==LEX_PLUSPLUS ? '+' : '-');
      jsvUnLock(one);
      jsvReplaceWith(a, res);
      jsvUnLock(res);
    }
  } else
    a = jspeFactorFunctionCall();
  return __jspePostfixExpression(a);
}
__attribute__ ((noinline)) JsVar *jspeUnaryExpression() {
  if (lex->tk=='!' || lex->tk=='~' || lex->tk=='-' || lex->tk=='+') {
    short tk = lex->tk;
    { do { if (!(0+lex->tk==(tk))) jsAssertFail("bin/espruino_embedded.c",10671,""); } while(0);jslGetNextToken(); };
    if (!(((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
      return jspeUnaryExpression();
    }
    JsVar *v = jsvSkipNameAndUnLock(jspeUnaryExpression());
    if (tk=='!') {
      return jsvNewFromBool(!jsvGetBoolAndUnLock(v));
    } else if (tk=='~') {
      return jsvNewFromInteger(~jsvGetIntegerAndUnLock(v));
    } else if (tk=='-') {
      return jsvNegateAndUnLock(v);
    } else if (tk=='+') {
      return jsvAsNumberAndUnLock(v);
    }
    do { if (!(0)) jsAssertFail("bin/espruino_embedded.c",10685,""); } while(0);
    return 0;
  } else
    return jspePostfixExpression();
}
unsigned int jspeGetBinaryExpressionPrecedence(int op) {
  switch (op) {
  case LEX_NULLISH:
  case LEX_OROR: return 1; break;
  case LEX_ANDAND: return 2; break;
  case '|' : return 3; break;
  case '^' : return 4; break;
  case '&' : return 5; break;
  case LEX_EQUAL:
  case LEX_NEQUAL:
  case LEX_TYPEEQUAL:
  case LEX_NTYPEEQUAL: return 6;
  case LEX_LEQUAL:
  case LEX_GEQUAL:
  case '<':
  case '>':
  case LEX_R_INSTANCEOF: return 7;
  case LEX_R_IN: return (execInfo.execute&EXEC_FOR_INIT)?0:7;
  case LEX_LSHIFT:
  case LEX_RSHIFT:
  case LEX_RSHIFTUNSIGNED: return 8;
  case '+':
  case '-': return 9;
  case '*':
  case '/':
  case '%': return 10;
  default: return 0;
  }
}
__attribute__ ((noinline)) JsVar *__jspeBinaryExpression(JsVar *a, unsigned int lastPrecedence) {
  unsigned int precedence = jspeGetBinaryExpressionPrecedence(lex->tk);
  while (precedence && precedence>lastPrecedence) {
    int op = lex->tk;
    { do { if (!(0+lex->tk==(op))) jsAssertFail("bin/espruino_embedded.c",10735,""); } while(0);jslGetNextToken(); };
    if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
      JsVar *an = jsvSkipNameAndUnLock(a);
      if (op!=LEX_R_INSTANCEOF) {
        a = jsvGetValueOf(an);
        jsvUnLock(an);
      } else
        a = an;
    }
    if (op==LEX_ANDAND || op==LEX_OROR) {
      bool aValue = jsvGetBool(a);
      if ((!aValue && op==LEX_ANDAND) ||
          (aValue && op==LEX_OROR)) {
        JsExecFlags oldExecute = execInfo.execute;
        jspSetNoExecute();
        jsvUnLock(__jspeBinaryExpression(jspeUnaryExpression(),precedence));
        execInfo.execute = (execInfo.execute&(JsExecFlags)(~EXEC_SAVE_RESTORE_MASK)) | (oldExecute&EXEC_SAVE_RESTORE_MASK);;
      } else {
        jsvUnLock(a);
        a = __jspeBinaryExpression(jspeUnaryExpression(),precedence);
      }
    } else if (op==LEX_NULLISH){
      if (jsvIsNullish(a)) {
        jsvUnLock(a);
        a = __jspeBinaryExpression(jspeUnaryExpression(),precedence);
      } else {
        JsExecFlags oldExecute = execInfo.execute;
        jspSetNoExecute();
        jsvUnLock(__jspeBinaryExpression(jspeUnaryExpression(),precedence));
        execInfo.execute = (execInfo.execute&(JsExecFlags)(~EXEC_SAVE_RESTORE_MASK)) | (oldExecute&EXEC_SAVE_RESTORE_MASK);;}
    } else {
      JsVar *b = __jspeBinaryExpression(jspeUnaryExpression(),precedence);
      if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
        if (op==LEX_R_IN) {
          JsVar *bv = jsvSkipName(b);
          if (jsvHasChildren(bv)) {
            JsVar *ai = jsvAsArrayIndexAndUnLock(a);
            JsVar *varFound = jspGetVarNamedField( bv, ai, true);
            a = jsvNewFromBool(varFound!=0);
            jsvUnLock2(ai, varFound);
          } else {
            const JswSymList *syms = jswGetSymbolListForObjectProto(bv);
            if (syms) {
              JsVar *varFound = 0;
              char nameBuf[64];
              if (jsvGetString(a, nameBuf, sizeof(nameBuf)) < sizeof(nameBuf))
                varFound = jswBinarySearch(syms, bv, nameBuf);
              bool found = varFound!=0;
              jsvUnLock(varFound);
              if (!found && jsvIsArrayBuffer(bv)) {
                JsVarFloat f = jsvGetFloat(a);
                if (f==floor(f) && f>=0 && f<jsvGetArrayBufferLength(bv))
                  found = true;
              }
              jsvUnLock(a);
              a = jsvNewFromBool(found);
            } else {
              jsExceptionHere(JSET_ERROR, "Can't use 'in' operator to search a %t", bv);
              jsvUnLock(a);
              a = 0;
            }
          }
          jsvUnLock(bv);
        } else if (op==LEX_R_INSTANCEOF) {
          bool inst = false;
          JsVar *bv = jsvSkipName(b);
          if (!jsvIsFunction(bv)) {
            jsExceptionHere(JSET_ERROR, "Expecting function on RHS, got %t", bv);
          } else {
            if (jsvIsObject(a) || jsvIsFunction(a)) {
              JsVar *bproto = jspGetNamedField(bv, "prototype", false);
              JsVar *proto = jsvObjectGetChildIfExists(a, "__proto__");
              while (jsvHasChildren(proto)) {
                if (proto == bproto) inst=true;
                JsVar *childProto = jsvObjectGetChildIfExists(proto, "__proto__");
                jsvUnLock(proto);
                proto = childProto;
              }
              if (jspIsConstructor(bv, "Object")) inst = true;
              jsvUnLock2(bproto, proto);
            }
            if (!inst) {
              const char *name = jswGetBasicObjectName(a);
              if (name) {
                inst = jspIsConstructor(bv, name);
              }
              if (!inst && (jsvIsArray(a) || jsvIsArrayBuffer(a)) &&
                  jspIsConstructor(bv, "Object"))
                inst = true;
            }
          }
          jsvUnLock2(a, bv);
          a = jsvNewFromBool(inst);
        } else {
          JsVar *pb = jsvSkipName(b);
          JsVar *bv = jsvGetValueOf(pb);
          jsvUnLock(pb);
          JsVar *res = jsvMathsOp(a,bv,op);
          jsvUnLock2(a,bv);
          a = res;
        }
      }
      jsvUnLock(b);
    }
    precedence = jspeGetBinaryExpressionPrecedence(lex->tk);
  }
  return a;
}
JsVar *jspeBinaryExpression() {
  return __jspeBinaryExpression(jspeUnaryExpression(),0);
}
__attribute__ ((noinline)) JsVar *__jspeConditionalExpression(JsVar *lhs) {
  if (lex->tk=='?') {
    { do { if (!(0+lex->tk==('?'))) jsAssertFail("bin/espruino_embedded.c",10861,""); } while(0);jslGetNextToken(); };
    if (!(((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
      jsvUnLock(jspeAssignmentExpression());
      { if (!jslMatch((':'))) { ; return 0; } };
      jsvUnLock(jspeAssignmentExpression());
    } else {
      bool first = jsvGetBoolAndUnLock(jsvSkipName(lhs));
      jsvUnLock(lhs);
      if (first) {
        lhs = jsvSkipNameAndUnLock(jspeAssignmentExpression());
        { if (!jslMatch((':'))) { ; return 0; } };
        JsExecFlags oldExecute = execInfo.execute;
        jspSetNoExecute();
        jsvUnLock(jspeAssignmentExpression());
        execInfo.execute = (execInfo.execute&(JsExecFlags)(~EXEC_SAVE_RESTORE_MASK)) | (oldExecute&EXEC_SAVE_RESTORE_MASK);;
      } else {
        JsExecFlags oldExecute = execInfo.execute;
        jspSetNoExecute();
        jsvUnLock(jspeAssignmentExpression());
        execInfo.execute = (execInfo.execute&(JsExecFlags)(~EXEC_SAVE_RESTORE_MASK)) | (oldExecute&EXEC_SAVE_RESTORE_MASK);;
        { if (!jslMatch((':'))) { ; return 0; } };
        lhs = jsvSkipNameAndUnLock(jspeAssignmentExpression());
      }
    }
  }
  return lhs;
}
JsVar *jspeConditionalExpression() {
  return __jspeConditionalExpression(jspeBinaryExpression());
}
__attribute__ ((noinline)) JsVar *__jspeAssignmentExpression(JsVar *lhs) {
  if (lex->tk=='=' || lex->tk==LEX_PLUSEQUAL || lex->tk==LEX_MINUSEQUAL ||
      lex->tk==LEX_MULEQUAL || lex->tk==LEX_DIVEQUAL || lex->tk==LEX_MODEQUAL ||
      lex->tk==LEX_ANDEQUAL || lex->tk==LEX_OREQUAL ||
      lex->tk==LEX_XOREQUAL || lex->tk==LEX_RSHIFTEQUAL ||
      lex->tk==LEX_LSHIFTEQUAL || lex->tk==LEX_RSHIFTUNSIGNEDEQUAL) {
    JsVar *rhs;
    int op = lex->tk;
    { do { if (!(0+lex->tk==(op))) jsAssertFail("bin/espruino_embedded.c",10904,""); } while(0);jslGetNextToken(); };
    rhs = jspeAssignmentExpression();
    rhs = jsvSkipNameAndUnLock(rhs);
    if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES) && lhs) {
      if (op=='=') {
        jsvReplaceWithOrAddToRoot(lhs, rhs);
      } else {
        if (op==LEX_PLUSEQUAL) op='+';
        else if (op==LEX_MINUSEQUAL) op='-';
        else if (op==LEX_MULEQUAL) op='*';
        else if (op==LEX_DIVEQUAL) op='/';
        else if (op==LEX_MODEQUAL) op='%';
        else if (op==LEX_ANDEQUAL) op='&';
        else if (op==LEX_OREQUAL) op='|';
        else if (op==LEX_XOREQUAL) op='^';
        else if (op==LEX_RSHIFTEQUAL) op=LEX_RSHIFT;
        else if (op==LEX_LSHIFTEQUAL) op=LEX_LSHIFT;
        else if (op==LEX_RSHIFTUNSIGNEDEQUAL) op=LEX_RSHIFTUNSIGNED;
        if (op=='+' && jsvIsName(lhs)) {
          JsVar *currentValue = jsvSkipName(lhs);
          if (jsvIsBasicString(currentValue) && jsvGetRefs(currentValue)==1 && rhs!=currentValue) {
            JsVar *str = jsvAsString(rhs);
            jsvAppendStringVarComplete(currentValue, str);
            jsvUnLock(str);
            op = 0;
          }
          jsvUnLock(currentValue);
        }
        if (op) {
          JsVar *res = jsvMathsOpSkipNames(lhs,rhs,op);
          jsvReplaceWith(lhs, res);
          jsvUnLock(res);
        }
      }
    }
    jsvUnLock(rhs);
    lhs = jsvSkipNameAndUnLock(lhs);
  }
  return lhs;
}
JsVar *jspeAssignmentExpression() {
  return __jspeAssignmentExpression(jspeConditionalExpression());
}
__attribute__ ((noinline)) JsVar *jspeExpression() {
  while (!(((execInfo.execute)&EXEC_NO_PARSE_MASK)!=0)) {
    JsVar *a = jspeAssignmentExpression();
    if (lex->tk!=',') return a;
    jsvCheckReferenceError(a);
    jsvUnLock(a);
    { do { if (!(0+lex->tk==(','))) jsAssertFail("bin/espruino_embedded.c",10963,""); } while(0);jslGetNextToken(); };
  }
  return 0;
}
__attribute__ ((noinline)) void jspeSkipBlock() {
  int brackets = 1;
  JsExecFlags oldExec = execInfo.execute;
  execInfo.execute = (JsExecFlags)(execInfo.execute & (JsExecFlags)~EXEC_RUN_MASK) | EXEC_NO;
  while (lex->tk && brackets) {
    if (lex->tk == '{') brackets++;
    else if (lex->tk == '}') {
      brackets--;
      if (!brackets) break;
    }
    { do { if (!(0+lex->tk==(lex->tk))) jsAssertFail("bin/espruino_embedded.c",10982,""); } while(0);jslGetNextToken(); };
  }
  execInfo.execute = oldExec;
}
__attribute__ ((noinline)) JsVar * jspeBlockStart() {
  execInfo.blockCount++;
  JsVar *oldBlockScope = execInfo.blockScope;
  execInfo.blockScope = 0;
  return oldBlockScope;
}
__attribute__ ((noinline)) void jspeBlockEnd(JsVar *oldBlockScope) {
  if (execInfo.blockScope) {
    jspeiRemoveScope();
    jsvUnLock(execInfo.blockScope);
    execInfo.blockScope = 0;
  }
  execInfo.blockScope = oldBlockScope;
  execInfo.blockCount--;
}
__attribute__ ((noinline)) void jspeBlockNoBrackets() {
  JsVar *oldBlockScope = jspeBlockStart();
  if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
    while (lex->tk && lex->tk!='}') {
      JsVar *a = jspeStatement();
      jsvCheckReferenceError(a);
      jsvUnLock(a);
      if ((((execInfo.execute)&EXEC_NO_PARSE_MASK)!=0))
        break;
      if (!(((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
        jspeSkipBlock();
        break;
      }
    }
  } else {
    jspeSkipBlock();
  }
  jspeBlockEnd(oldBlockScope);
}
__attribute__ ((noinline)) void jspeBlock() {
  { if (!jslMatch(('{'))) { ; return ; } };
  jspeBlockNoBrackets();
  if (!(((execInfo.execute)&EXEC_NO_PARSE_MASK)!=0)) { if (!jslMatch(('}'))) { ; return ; } };
  return;
}
__attribute__ ((noinline)) JsVar *jspeBlockOrStatement() {
  if (lex->tk=='{') {
    jspeBlock();
    return 0;
  } else {
    JsVar *v = jspeStatement();
    return v;
  }
}
__attribute__ ((noinline)) JsVar *jspParse() {
  JsVar *v = 0;
  while (!(((execInfo.execute)&EXEC_NO_PARSE_MASK)!=0) && lex->tk != LEX_EOF) {
    jsvUnLock(v);
    v = jspeBlockOrStatement();
    while (lex->tk==';') { do { if (!(0+lex->tk==(';'))) jsAssertFail("bin/espruino_embedded.c",11059,""); } while(0);jslGetNextToken(); };
    jsvCheckReferenceError(v);
  }
  return v;
}
__attribute__ ((noinline)) JsVar *jspeStatementVar() {
  JsVar *lastDefined = 0;
  do { if (!(lex->tk==LEX_R_VAR || lex->tk==LEX_R_LET || lex->tk==LEX_R_CONST)) jsAssertFail("bin/espruino_embedded.c",11070,""); } while(0);
  bool isBlockScoped = (lex->tk==LEX_R_LET || lex->tk==LEX_R_CONST) && execInfo.blockCount;
  bool isConstant = lex->tk==LEX_R_CONST;
  jslGetNextToken();
  bool hasComma = true;
  while (hasComma && lex->tk == LEX_ID && !jspIsInterrupted()) {
    JsVar *a = 0;
    if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
      char *name = jslGetTokenValueAsString();
      if (isBlockScoped) {
        if (!execInfo.blockScope) {
          execInfo.blockScope = jsvNewObject();
          jspeiAddScope(execInfo.blockScope);
        }
        a = jsvFindOrAddChildFromString(execInfo.blockScope, name);
      } else {
        a = jsvFindOrAddChildFromString(execInfo.baseScope, name);
      }
      if (!a) {
        jspSetError();
        return lastDefined;
      }
    }
    { if (!jslMatch((LEX_ID))) { jsvUnLock(a); return lastDefined; } };
    if (lex->tk == '=') {
      JsVar *var;
      { if (!jslMatch(('='))) { jsvUnLock(a); return lastDefined; } };
      var = jsvSkipNameAndUnLock(jspeAssignmentExpression());
      if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES))
        jsvReplaceWith(a, var);
      jsvUnLock(var);
    }
    if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
      if (isConstant)
        a->flags |= JSV_CONSTANT;
    }
    jsvUnLock(lastDefined);
    lastDefined = a;
    hasComma = lex->tk == ',';
    if (hasComma) { if (!jslMatch((','))) { ; return lastDefined; } };
  }
  return lastDefined;
}
__attribute__ ((noinline)) JsVar *jspeStatementIf() {
  bool cond;
  JsVar *var, *result = 0;
  { do { if (!(0+lex->tk==(LEX_R_IF))) jsAssertFail("bin/espruino_embedded.c",11129,""); } while(0);jslGetNextToken(); };
  { if (!jslMatch(('('))) { ; return 0; } };
  var = jspeExpression();
  if ((((execInfo.execute)&EXEC_NO_PARSE_MASK)!=0)) return var;
  { if (!jslMatch((')'))) { ; return 0; } };
  cond = (((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES) && jsvGetBoolAndUnLock(jsvSkipName(var));
  jsvUnLock(var);
  JsExecFlags oldExecute = execInfo.execute;
  if (!cond) jspSetNoExecute();
  JsExecFlags hasError = 0;
  JsVar *a = 0;
  if (lex->tk!=';')
    a = jspeBlockOrStatement();
  hasError |= execInfo.execute&EXEC_ERROR_MASK;
  if (!cond) {
    jsvUnLock(a);
    execInfo.execute = (execInfo.execute&(JsExecFlags)(~EXEC_SAVE_RESTORE_MASK)) | (oldExecute&EXEC_SAVE_RESTORE_MASK);;
    execInfo.execute |= hasError;
  } else {
    result = a;
  }
  if (lex->tk==';') { do { if (!(0+lex->tk==(';'))) jsAssertFail("bin/espruino_embedded.c",11153,""); } while(0);jslGetNextToken(); };
  if (lex->tk==LEX_R_ELSE) {
    { do { if (!(0+lex->tk==(LEX_R_ELSE))) jsAssertFail("bin/espruino_embedded.c",11155,""); } while(0);jslGetNextToken(); };
    JsExecFlags oldExecute = execInfo.execute;
    if (cond) jspSetNoExecute();
    JsVar *a = 0;
    if (lex->tk!=';')
      a = jspeBlockOrStatement();
    hasError |= execInfo.execute&EXEC_ERROR_MASK;
    if (cond) {
      jsvUnLock(a);
      execInfo.execute = (execInfo.execute&(JsExecFlags)(~EXEC_SAVE_RESTORE_MASK)) | (oldExecute&EXEC_SAVE_RESTORE_MASK);;
      execInfo.execute |= hasError;
    } else {
      result = a;
    }
  }
  return result;
}
__attribute__ ((noinline)) JsVar *jspeStatementSwitch() {
  { do { if (!(0+lex->tk==(LEX_R_SWITCH))) jsAssertFail("bin/espruino_embedded.c",11174,""); } while(0);jslGetNextToken(); };
  { if (!jslMatch(('('))) { ; return 0; } };
  JsVar *switchOn = jspeExpression();
  JsExecFlags preservedExecState = execInfo.execute&(EXEC_IN_LOOP|EXEC_DEBUGGER_MASK);
  JsExecFlags oldExecute = execInfo.execute;
  bool execute = (((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES);
  { if (!jslMatch((')'))) { jsvUnLock(switchOn); return 0; } };
  if (!execute) { jsvUnLock(switchOn); jspeBlock(); return 0; }
  { if (!jslMatch(('{'))) { jsvUnLock(switchOn); return 0; } };
  bool executeDefault = true;
  if (execute) execInfo.execute=EXEC_NO|EXEC_IN_SWITCH|preservedExecState;
  while (lex->tk==LEX_R_CASE) {
    JsExecFlags oldFlags = execInfo.execute;
    if (execute) execInfo.execute=EXEC_YES|EXEC_IN_SWITCH|preservedExecState;
    { if (!jslMatch((LEX_R_CASE))) { jsvUnLock(switchOn); return 0; } };
    JsVar *test = jspeAssignmentExpression();
    execInfo.execute = oldFlags|EXEC_IN_SWITCH;
    { if (!jslMatch((':'))) { jsvUnLock2(switchOn, test); return 0; } };
    bool cond = false;
    if (execute)
      cond = jsvGetBoolAndUnLock(jsvMathsOpSkipNames(switchOn, test, LEX_TYPEEQUAL));
    if (cond) executeDefault = false;
    jsvUnLock(test);
    if (cond && (execInfo.execute&EXEC_RUN_MASK)==EXEC_NO)
      execInfo.execute=EXEC_YES|EXEC_IN_SWITCH|preservedExecState;
    while (!(((execInfo.execute)&EXEC_NO_PARSE_MASK)!=0) && lex->tk!=LEX_EOF && lex->tk!=LEX_R_CASE && lex->tk!=LEX_R_DEFAULT && lex->tk!='}')
      jsvUnLock(jspeBlockOrStatement());
    oldExecute |= execInfo.execute & (EXEC_ERROR_MASK|EXEC_RETURN|EXEC_CONTINUE);
  }
  jsvUnLock(switchOn);
  if (execute && (execInfo.execute&EXEC_RUN_MASK)==EXEC_BREAK) {
    execInfo.execute=EXEC_YES|EXEC_IN_SWITCH|preservedExecState;
  } else {
    executeDefault = true;
  }
  execInfo.execute = (execInfo.execute&(JsExecFlags)(~EXEC_SAVE_RESTORE_MASK)) | (oldExecute&EXEC_SAVE_RESTORE_MASK);;
  if (lex->tk==LEX_R_DEFAULT) {
    { do { if (!(0+lex->tk==(LEX_R_DEFAULT))) jsAssertFail("bin/espruino_embedded.c",11220,""); } while(0);jslGetNextToken(); };
    { if (!jslMatch((':'))) { ; return 0; } };
    JsExecFlags oldExecute = execInfo.execute;
    if (!executeDefault) jspSetNoExecute();
    else execInfo.execute |= EXEC_IN_SWITCH;
    while (!(((execInfo.execute)&EXEC_NO_PARSE_MASK)!=0) && lex->tk!=LEX_EOF && lex->tk!='}' && lex->tk!=LEX_R_CASE)
      jsvUnLock(jspeBlockOrStatement());
    oldExecute |= execInfo.execute & (EXEC_ERROR_MASK|EXEC_RETURN|EXEC_CONTINUE);
    execInfo.execute = execInfo.execute & (JsExecFlags)~EXEC_BREAK;
    execInfo.execute = (execInfo.execute&(JsExecFlags)(~EXEC_SAVE_RESTORE_MASK)) | (oldExecute&EXEC_SAVE_RESTORE_MASK);;
  }
  if (lex->tk==LEX_R_CASE) {
    jsExceptionHere(JSET_SYNTAXERROR, "CASE after DEFAULT unsupported");
    return 0;
  }
  { if (!jslMatch(('}'))) { ; return 0; } };
  return 0;
}
static __attribute__ ((noinline)) bool jspeCheckBreakContinue() {
  if (execInfo.execute & EXEC_CONTINUE)
    execInfo.execute = (execInfo.execute & (JsExecFlags)~EXEC_RUN_MASK) | EXEC_YES;
  else if (execInfo.execute & EXEC_BREAK) {
    execInfo.execute = (execInfo.execute & (JsExecFlags)~EXEC_RUN_MASK) | EXEC_YES;
    return true;
  }
  return false;
}
__attribute__ ((noinline)) JsVar *jspeStatementDoOrWhile(bool isWhile) {
  JsVar *cond;
  bool loopCond = true;
  bool hasHadBreak = false;
  JslCharPos whileCondStart;
  bool wasInLoop = (execInfo.execute&EXEC_IN_LOOP)!=0;
  JslCharPos whileBodyStart;
  if (isWhile) {
    { do { if (!(0+lex->tk==(LEX_R_WHILE))) jsAssertFail("bin/espruino_embedded.c",11260,""); } while(0);jslGetNextToken(); };
    jslCharPosFromLex(&whileCondStart);
    { if (!jslMatch(('('))) { jslCharPosFree(&whileCondStart);; return 0; } };
    cond = jspeExpression();
    loopCond = (((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES) && jsvGetBoolAndUnLock(jsvSkipName(cond));
    jsvUnLock(cond);
    jslCharPosFromLex(&whileBodyStart);
    { if (!jslMatch((')'))) { jslCharPosFree(&whileBodyStart);jslCharPosFree(&whileCondStart);; return 0; } };
  } else {
    jslCharPosFromLex(&whileBodyStart);
    { if (!jslMatch((LEX_R_DO))) { jslCharPosFree(&whileBodyStart);; return 0; } };
    jslCharPosClear(&whileCondStart);
  }
  JsExecFlags oldExecute = execInfo.execute;
  if (!loopCond) jspSetNoExecute();
  execInfo.execute |= EXEC_IN_LOOP;
  bool needSemiColon = (!isWhile) && lex->tk!='{';
  jsvUnLock(jspeBlockOrStatement());
  if (needSemiColon) { if (!jslMatch((';'))) { jslCharPosFree(&whileBodyStart);jslCharPosFree(&whileCondStart);; return 0; } };
  if (!wasInLoop) execInfo.execute &= (JsExecFlags)~EXEC_IN_LOOP;
  hasHadBreak |= jspeCheckBreakContinue();
  if (!loopCond) execInfo.execute = (execInfo.execute&(JsExecFlags)(~EXEC_SAVE_RESTORE_MASK)) | (oldExecute&EXEC_SAVE_RESTORE_MASK);;
  if (!isWhile) {
    { if (!jslMatch((LEX_R_WHILE))) { jslCharPosFree(&whileBodyStart);; return 0; } };
    jslCharPosFromLex(&whileCondStart);
    { if (!jslMatch(('('))) { jslCharPosFree(&whileBodyStart);jslCharPosFree(&whileCondStart);; return 0; } };
    cond = jspeExpression();
    loopCond = (((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES) && jsvGetBoolAndUnLock(jsvSkipName(cond));
    jsvUnLock(cond);
    { if (!jslMatch((')'))) { jslCharPosFree(&whileBodyStart);jslCharPosFree(&whileCondStart);; return 0; } };
  }
  JslCharPos whileBodyEnd;
  jslCharPosNew(&whileBodyEnd, lex->sourceVar, lex->tokenStart);
  int loopCount = 0;
  while (!hasHadBreak && loopCond
  ) {
    if (isWhile || loopCount) {
      jslSeekToP(&whileCondStart);
      cond = jspeExpression();
      loopCond = (((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES) && jsvGetBoolAndUnLock(jsvSkipName(cond));
      jsvUnLock(cond);
    }
    if (loopCond) {
      jslSeekToP(&whileBodyStart);
      execInfo.execute |= EXEC_IN_LOOP;
      jspDebuggerLoopIfCtrlC();
      jsvUnLock(jspeBlockOrStatement());
      if (!wasInLoop) execInfo.execute &= (JsExecFlags)~EXEC_IN_LOOP;
      hasHadBreak |= jspeCheckBreakContinue();
    }
    loopCount++;
  }
  jslSeekToP(&whileBodyEnd);
  jslCharPosFree(&whileCondStart);
  jslCharPosFree(&whileBodyStart);
  jslCharPosFree(&whileBodyEnd);
  return 0;
}
__attribute__ ((noinline)) JsVar *jspGetBuiltinPrototype(JsVar *obj) {
  if (jsvIsArray(obj)) {
    JsVar *v = jspFindPrototypeFor("Array");
    if (v) return v;
  }
  if (jsvIsObject(obj) || jsvIsArray(obj)) {
    JsVar *v = jspFindPrototypeFor("Object");
    if (v==obj) {
      jsvUnLock(v);
      v = 0;
    }
    return v;
  }
  return 0;
}
__attribute__ ((noinline)) JsVar *jspeStatementFor() {
  { do { if (!(0+lex->tk==(LEX_R_FOR))) jsAssertFail("bin/espruino_embedded.c",11349,""); } while(0);jslGetNextToken(); };
  { if (!jslMatch(('('))) { ; return 0; } };
  bool wasInLoop = (execInfo.execute&EXEC_IN_LOOP)!=0;
  execInfo.execute |= EXEC_FOR_INIT;
  JsVar *oldBlockScope = jspeBlockStart();
  JsVar *forStatement = 0;
  bool startsWithConst = lex->tk==LEX_R_CONST;
  if (lex->tk != ';')
    forStatement = jspeStatement();
  if (jspIsInterrupted()) {
    jsvUnLock(forStatement);
    jspeBlockEnd(oldBlockScope);
    return 0;
  }
  execInfo.execute &= (JsExecFlags)~EXEC_FOR_INIT;
  if (lex->tk == LEX_R_IN || lex->tk == LEX_R_OF) {
    bool isForOf = lex->tk == LEX_R_OF;
    if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES) && !jsvIsName(forStatement)) {
      jsvUnLock(forStatement);
      jsExceptionHere(JSET_ERROR, "for(a %s b) - 'a' must be a variable name, not %t", isForOf?"of":"in", forStatement);
      jspeBlockEnd(oldBlockScope);
      return 0;
    }
    { do { if (!(0+lex->tk==(lex->tk))) jsAssertFail("bin/espruino_embedded.c",11378,""); } while(0);jslGetNextToken(); };
    JsVar *array = jsvSkipNameAndUnLock(jspeExpression());
    JslCharPos forBodyStart;
    jslCharPosFromLex(&forBodyStart);
    { if (!jslMatch((')'))) { jsvUnLock2(forStatement, array);jslCharPosFree(&forBodyStart);jspeBlockEnd(oldBlockScope);; return 0; } };
    JsExecFlags oldExecute = execInfo.execute;
    jspSetNoExecute();
    execInfo.execute |= EXEC_IN_LOOP;
    jsvUnLock(jspeBlockOrStatement());
    JslCharPos forBodyEnd;
    jslCharPosNew(&forBodyEnd, lex->sourceVar, lex->tokenStart);
    if (!wasInLoop) execInfo.execute &= (JsExecFlags)~EXEC_IN_LOOP;
    execInfo.execute = (execInfo.execute&(JsExecFlags)(~EXEC_SAVE_RESTORE_MASK)) | (oldExecute&EXEC_SAVE_RESTORE_MASK);;
    if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
      if (jsvIsIterable(array)) {
        JsvIsInternalChecker checkerFunction = jsvGetInternalFunctionCheckerFor(array);
        JsVar *foundPrototype = 0;
        if (!isForOf)
          foundPrototype = jspGetBuiltinPrototype(array);
        JsvIterator it;
        jsvIteratorNew(&it, array, isForOf ?
                         JSIF_EVERY_ARRAY_ELEMENT :
                         JSIF_DEFINED_ARRAY_ElEMENTS);
        bool hasHadBreak = false;
        while ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES) && jsvIteratorHasElement(&it) && !hasHadBreak) {
          JsVar *loopIndexVar = jsvIteratorGetKey(&it);
          bool ignore = false;
          if (checkerFunction && checkerFunction(loopIndexVar)) {
            ignore = true;
            if (!isForOf &&
                jsvIsString(loopIndexVar) &&
                jsvIsStringEqual(loopIndexVar, "__proto__"))
              foundPrototype = jsvSkipName(loopIndexVar);
          }
          if (!ignore) {
            JsVar *iteratorValue;
            if (isForOf) {
              iteratorValue = jsvIteratorGetValue(&it);
            } else {
              iteratorValue = jsvAsString(loopIndexVar);
              do { if (!(jsvGetRefs(iteratorValue)==0)) jsAssertFail("bin/espruino_embedded.c",11424,""); } while(0);
            }
            if (isForOf || iteratorValue) {
              do { if (!(!jsvIsName(iteratorValue))) jsAssertFail("bin/espruino_embedded.c",11428,""); } while(0);
              if (startsWithConst) forStatement->flags &= ~JSV_CONSTANT;
              jsvReplaceWithOrAddToRoot(forStatement, iteratorValue);
              if (startsWithConst) forStatement->flags |= JSV_CONSTANT;
              jsvUnLock(iteratorValue);
              jslSeekToP(&forBodyStart);
              execInfo.execute |= EXEC_IN_LOOP;
              jspDebuggerLoopIfCtrlC();
              jsvUnLock(jspeBlockOrStatement());
              if (!wasInLoop) execInfo.execute &= (JsExecFlags)~EXEC_IN_LOOP;
              hasHadBreak |= jspeCheckBreakContinue();
            }
          }
          jsvIteratorNext(&it);
          jsvUnLock(loopIndexVar);
          if (!jsvIteratorHasElement(&it) && !isForOf && foundPrototype) {
            jsvIteratorFree(&it);
            JsVar *iterable = foundPrototype;
            jsvIteratorNew(&it, iterable, JSIF_DEFINED_ARRAY_ElEMENTS);
            checkerFunction = jsvGetInternalFunctionCheckerFor(iterable);
            foundPrototype = jspGetBuiltinPrototype(iterable);
            jsvUnLock(iterable);
          }
        }
        jsvUnLock(foundPrototype);
        jsvIteratorFree(&it);
      } else if (!jsvIsUndefined(array)) {
        jsExceptionHere(JSET_ERROR, "FOR loop can only iterate over Arrays, Strings or Objects, not %t", array);
      }
    }
    jslSeekToP(&forBodyEnd);
    jslCharPosFree(&forBodyStart);
    jslCharPosFree(&forBodyEnd);
    jsvUnLock2(forStatement, array);
  } else {
    bool loopCond = true;
    bool hasHadBreak = false;
    jsvUnLock(forStatement);
    JslCharPos forCondStart;
    jslCharPosFromLex(&forCondStart);
    { if (!jslMatch((';'))) { jslCharPosFree(&forCondStart);jspeBlockEnd(oldBlockScope);jspeBlockEnd(oldBlockScope);; return 0; } };
    if (lex->tk != ';') {
      JsVar *cond = jspeExpression();
      loopCond = (((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES) && jsvGetBoolAndUnLock(jsvSkipName(cond));
      jsvUnLock(cond);
    }
    JslCharPos forIterStart;
    jslCharPosFromLex(&forIterStart);
    { if (!jslMatch((';'))) { jslCharPosFree(&forCondStart);jslCharPosFree(&forIterStart);jspeBlockEnd(oldBlockScope);; return 0; } };
    if (lex->tk != ')') {
      JsExecFlags oldExecute = execInfo.execute;
      jspSetNoExecute();
      jsvUnLock(jspeExpression());
      execInfo.execute = (execInfo.execute&(JsExecFlags)(~EXEC_SAVE_RESTORE_MASK)) | (oldExecute&EXEC_SAVE_RESTORE_MASK);;
    }
    JslCharPos forBodyStart;
    jslSkipWhiteSpace();
    jslCharPosFromLex(&forBodyStart);
    { if (!jslMatch((')'))) { jslCharPosFree(&forCondStart);jslCharPosFree(&forIterStart);jslCharPosFree(&forBodyStart);jspeBlockEnd(oldBlockScope);; return 0; } };
    JsExecFlags oldExecute = execInfo.execute;
    if (!loopCond) jspSetNoExecute();
    execInfo.execute |= EXEC_IN_LOOP;
    jsvUnLock(jspeBlockOrStatement());
    JslCharPos forBodyEnd;
    jslSkipWhiteSpace();
    jslCharPosNew(&forBodyEnd, lex->sourceVar, lex->tokenStart);
    if (!wasInLoop) execInfo.execute &= (JsExecFlags)~EXEC_IN_LOOP;
    if (loopCond || !(((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
      hasHadBreak |= jspeCheckBreakContinue();
      if (hasHadBreak) loopCond = false;
    }
    if (!loopCond) execInfo.execute = (execInfo.execute&(JsExecFlags)(~EXEC_SAVE_RESTORE_MASK)) | (oldExecute&EXEC_SAVE_RESTORE_MASK);;
    if (loopCond) {
      jslSeekToP(&forIterStart);
      if (lex->tk != ')') jsvUnLock(jspeExpression());
    }
    while (!hasHadBreak && (((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES) && loopCond
    ) {
      jslSeekToP(&forCondStart);
      if (lex->tk == ';') {
        loopCond = true;
      } else {
        JsVar *cond = jspeExpression();
        loopCond = jsvGetBoolAndUnLock(jsvSkipName(cond));
        jsvUnLock(cond);
      }
      if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES) && loopCond) {
        jslSeekToP(&forBodyStart);
        execInfo.execute |= EXEC_IN_LOOP;
        jspDebuggerLoopIfCtrlC();
        jsvUnLock(jspeBlockOrStatement());
        if (!wasInLoop) execInfo.execute &= (JsExecFlags)~EXEC_IN_LOOP;
        hasHadBreak |= jspeCheckBreakContinue();
      }
      if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES) && loopCond && !hasHadBreak) {
        jslSeekToP(&forIterStart);
        if (lex->tk != ')') jsvUnLock(jspeExpression());
      }
    }
    jslSeekToP(&forBodyEnd);
    jslCharPosFree(&forCondStart);
    jslCharPosFree(&forIterStart);
    jslCharPosFree(&forBodyStart);
    jslCharPosFree(&forBodyEnd);
  }
  jspeBlockEnd(oldBlockScope);
  return 0;
}
__attribute__ ((noinline)) JsVar *jspeStatementTry() {
  { do { if (!(0+lex->tk==(LEX_R_TRY))) jsAssertFail("bin/espruino_embedded.c",11562,""); } while(0);jslGetNextToken(); };
  bool shouldExecuteBefore = (((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES);
  jspeBlock();
  bool hadException = shouldExecuteBefore && ((execInfo.execute & EXEC_EXCEPTION)!=0);
  bool hadCatch = false;
  if (lex->tk == LEX_R_CATCH) {
    { do { if (!(0+lex->tk==(LEX_R_CATCH))) jsAssertFail("bin/espruino_embedded.c",11569,""); } while(0);jslGetNextToken(); };
    hadCatch = true;
    JsVar *exceptionVar = 0;
    JsVar *scope = 0;
    JsVar *exception = shouldExecuteBefore ? jspGetException() : 0;
    if (lex->tk == '(') {
      { if (!jslMatch(('('))) { ; return 0; } };
      if (hadException) {
        scope = jsvNewObject();
        if (scope)
          exceptionVar = jsvFindOrAddChildFromString(scope, jslGetTokenValueAsString());
      }
      { if (!jslMatch((LEX_ID))) { jsvUnLock2(scope,exceptionVar); return 0; } };
      { if (!jslMatch((')'))) { jsvUnLock2(scope,exceptionVar); return 0; } };
      if (exceptionVar) {
        if (exception)
          jsvSetValueOfName(exceptionVar, exception);
        jsvUnLock(exceptionVar);
      }
    }
    if (shouldExecuteBefore) {
      execInfo.execute = execInfo.execute & (JsExecFlags)~EXEC_EXCEPTION;
      jsvUnLock(exception);
    }
    if (shouldExecuteBefore && !hadException) {
      JsExecFlags oldExecute = execInfo.execute;
      jspSetNoExecute();
      jspeBlock();
      execInfo.execute = (execInfo.execute&(JsExecFlags)(~EXEC_SAVE_RESTORE_MASK)) | (oldExecute&EXEC_SAVE_RESTORE_MASK);;
    } else {
      if (!scope || jspeiAddScope(scope)) {
        jspeBlock();
        if (scope) jspeiRemoveScope();
      }
    }
    jsvUnLock(scope);
  }
  if (lex->tk == LEX_R_FINALLY || (!hadCatch && ((execInfo.execute&(EXEC_ERROR|EXEC_INTERRUPTED))==0))) {
    { if (!jslMatch((LEX_R_FINALLY))) { ; return 0; } };
    JsExecFlags oldExec = execInfo.execute;
    if (shouldExecuteBefore)
      execInfo.execute = (execInfo.execute & (JsExecFlags)~(EXEC_EXCEPTION|EXEC_RETURN|EXEC_BREAK|EXEC_CONTINUE)) | EXEC_YES;
    jspeBlock();
    execInfo.execute = oldExec;
    if (hadException && !hadCatch) execInfo.execute = execInfo.execute | EXEC_EXCEPTION;
  }
  return 0;
}
__attribute__ ((noinline)) JsVar *jspeStatementReturn() {
  JsVar *result = 0;
  { do { if (!(0+lex->tk==(LEX_R_RETURN))) jsAssertFail("bin/espruino_embedded.c",11627,""); } while(0);jslGetNextToken(); };
  if (lex->tk != ';' && lex->tk != '}') {
    result = jsvSkipNameAndUnLock(jspeExpression());
  }
  if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
    JsVar *resultVar = jspeiFindInScopes("\xFF""rtn");
    if (resultVar) {
      jsvReplaceWith(resultVar, result);
      jsvUnLock(resultVar);
      execInfo.execute |= EXEC_RETURN;
    } else {
      jsExceptionHere(JSET_SYNTAXERROR, "RETURN statement, but not in a function.");
    }
  }
  jsvUnLock(result);
  return 0;
}
__attribute__ ((noinline)) JsVar *jspeStatementThrow() {
  JsVar *result = 0;
  { do { if (!(0+lex->tk==(LEX_R_THROW))) jsAssertFail("bin/espruino_embedded.c",11648,""); } while(0);jslGetNextToken(); };
  result = jsvSkipNameAndUnLock(jspeExpression());
  if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
    jspSetException(result);
  }
  jsvUnLock(result);
  return 0;
}
__attribute__ ((noinline)) JsVar *jspeStatementFunctionDecl(bool isClass) {
  JsVar *funcName = 0;
  JsVar *funcVar;
  { do { if (!(0+lex->tk==(isClass ? LEX_R_CLASS : LEX_R_FUNCTION))) jsAssertFail("bin/espruino_embedded.c",11662,""); } while(0);jslGetNextToken(); };
  bool actuallyCreateFunction = (((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES);
  if (actuallyCreateFunction) {
    funcName = jsvNewNameFromString(jslGetTokenValueAsString());
    if (!funcName) {
      return 0;
    }
  }
  { if (!jslMatch((LEX_ID))) { jsvUnLock(funcName); return 0; } };
  funcVar = isClass ? jspeClassDefinition(false) : jspeFunctionDefinition(false);
  if (actuallyCreateFunction) {
    JsVar *existingName = jsvFindChildFromVar(execInfo.baseScope, funcName, true);
    JsVar *existingFunc = jsvSkipName(existingName);
    if (jsvIsFunction(existingFunc)) {
      funcVar = jsvSkipNameAndUnLock(funcVar);
      jswrap_function_replaceWith(existingFunc, funcVar);
    } else {
      jsvReplaceWith(existingName, funcVar);
    }
    jsvUnLock(funcName);
    funcName = existingName;
    jsvUnLock(existingFunc);
  }
  jsvUnLock(funcVar);
  return funcName;
}
__attribute__ ((noinline)) JsVar *jspeStatement() {
  if (lex->tk==LEX_ID ||
      lex->tk==LEX_INT ||
      lex->tk==LEX_FLOAT ||
      lex->tk==LEX_STR ||
      lex->tk==LEX_TEMPLATE_LITERAL ||
      lex->tk==LEX_REGEX ||
      lex->tk==LEX_R_NEW ||
      lex->tk==LEX_R_NULL ||
      lex->tk==LEX_R_UNDEFINED ||
      lex->tk==LEX_R_TRUE ||
      lex->tk==LEX_R_FALSE ||
      lex->tk==LEX_R_THIS ||
      lex->tk==LEX_R_DELETE ||
      lex->tk==LEX_R_TYPEOF ||
      lex->tk==LEX_R_VOID ||
      lex->tk==LEX_R_SUPER ||
      lex->tk==LEX_PLUSPLUS ||
      lex->tk==LEX_MINUSMINUS ||
      lex->tk=='!' ||
      lex->tk=='-' ||
      lex->tk=='+' ||
      lex->tk=='~' ||
      lex->tk=='[' ||
      lex->tk=='(') {
    return jspeExpression();
  } else if (lex->tk=='{') {
    if (!jspCheckStackPosition()) return 0;
    jspeBlock();
    return 0;
  } else if (lex->tk==';') {
    { do { if (!(0+lex->tk==(';'))) jsAssertFail("bin/espruino_embedded.c",11749,""); } while(0);jslGetNextToken(); };
    return 0;
  } else if (lex->tk==LEX_R_VAR ||
            lex->tk==LEX_R_LET ||
            lex->tk==LEX_R_CONST) {
    return jspeStatementVar();
  } else if (lex->tk==LEX_R_IF) {
    return jspeStatementIf();
  } else if (lex->tk==LEX_R_DO) {
    return jspeStatementDoOrWhile(false);
  } else if (lex->tk==LEX_R_WHILE) {
    return jspeStatementDoOrWhile(true);
  } else if (lex->tk==LEX_R_FOR) {
    return jspeStatementFor();
  } else if (lex->tk==LEX_R_TRY) {
    return jspeStatementTry();
  } else if (lex->tk==LEX_R_RETURN) {
    return jspeStatementReturn();
  } else if (lex->tk==LEX_R_THROW) {
    return jspeStatementThrow();
  } else if (lex->tk==LEX_R_FUNCTION) {
    return jspeStatementFunctionDecl(false );
  } else if (lex->tk==LEX_R_CLASS) {
      return jspeStatementFunctionDecl(true );
  } else if (lex->tk==LEX_R_CONTINUE) {
    { do { if (!(0+lex->tk==(LEX_R_CONTINUE))) jsAssertFail("bin/espruino_embedded.c",11776,""); } while(0);jslGetNextToken(); };
    if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
      if (!(execInfo.execute & EXEC_IN_LOOP))
        jsExceptionHere(JSET_SYNTAXERROR, "CONTINUE statement outside of FOR or WHILE loop");
      else
        execInfo.execute = (execInfo.execute & (JsExecFlags)~EXEC_RUN_MASK) | EXEC_CONTINUE;
    }
  } else if (lex->tk==LEX_R_BREAK) {
    { do { if (!(0+lex->tk==(LEX_R_BREAK))) jsAssertFail("bin/espruino_embedded.c",11784,""); } while(0);jslGetNextToken(); };
    if ((((execInfo.execute)&EXEC_RUN_MASK)==EXEC_YES)) {
      if (!(execInfo.execute & (EXEC_IN_LOOP|EXEC_IN_SWITCH)))
        jsExceptionHere(JSET_SYNTAXERROR, "BREAK statement outside of SWITCH, FOR or WHILE loop");
      else
        execInfo.execute = (execInfo.execute & (JsExecFlags)~EXEC_RUN_MASK) | EXEC_BREAK;
    }
  } else if (lex->tk==LEX_R_SWITCH) {
    return jspeStatementSwitch();
  } else if (lex->tk==LEX_R_DEBUGGER) {
    { do { if (!(0+lex->tk==(LEX_R_DEBUGGER))) jsAssertFail("bin/espruino_embedded.c",11794,""); } while(0);jslGetNextToken(); };
  } else { if (!jslMatch((LEX_EOF))) { ; return 0; } };
  return 0;
}
JsVar *jspNewBuiltin(const char *instanceOf) {
  JsVar *objFunc = jswFindBuiltInFunction(0, instanceOf);
  if (!objFunc) return 0;
  return objFunc;
}
__attribute__ ((noinline)) JsVar *jspNewPrototype(const char *instanceOf, bool returnObject) {
  JsVar *objFuncName = jsvFindOrAddChildFromString(execInfo.root, instanceOf);
  if (!objFuncName)
    return 0;
  JsVar *objFunc = jsvSkipName(objFuncName);
  if (!objFunc) {
    objFunc = jspNewBuiltin(instanceOf);
    if (!objFunc) {
      jsvUnLock(objFuncName);
      return 0;
    }
    jsvSetValueOfName(objFuncName, objFunc);
  }
  JsVar *prototypeName = jsvFindOrAddChildFromString(objFunc, "prototype");
  jspEnsureIsPrototype(objFunc, prototypeName);
  jsvUnLock2(returnObject ? prototypeName : objFunc, objFuncName);
  return returnObject ? objFunc : prototypeName;
}
__attribute__ ((noinline)) JsVar *jspNewObject(const char *name, const char *instanceOf) {
  JsVar *prototypeName = jspNewPrototype(instanceOf, false );
  JsVar *obj = jsvNewObject();
  if (!obj) {
    jsvUnLock(prototypeName);
    return 0;
  }
  jsvAddNamedChildAndUnLock(obj, jsvSkipNameAndUnLock(prototypeName), "__proto__");
  if (name) {
    JsVar *objName = jsvFindOrAddChildFromString(execInfo.root, name);
    if (objName) jsvSetValueOfName(objName, obj);
    jsvUnLock(obj);
    if (!objName) {
      return 0;
    }
    return objName;
  } else
    return obj;
}
bool jspIsConstructor(JsVar *constructor, const char *constructorName) {
  JsVar *objFunc = jsvObjectGetChildIfExists(execInfo.root, constructorName);
  if (!objFunc) return false;
  bool isConstructor = objFunc == constructor;
  jsvUnLock(objFunc);
  return isConstructor;
}
JsVar *jspGetPrototype(JsVar *object) {
  if (!jsvIsObject(object)) return 0;
  JsVar *proto = jsvObjectGetChildIfExists(object, "__proto__");
  if (jsvIsObject(proto))
    return proto;
  jsvUnLock(proto);
  return 0;
}
JsVar *jspGetConstructor(JsVar *object) {
  JsVar *proto = jspGetPrototype(object);
  if (proto) {
    JsVar *constr = jsvObjectGetChildIfExists(proto, "constructor");
    if (jsvIsFunction(constr)) {
      jsvUnLock(proto);
      return constr;
    }
    jsvUnLock2(constr, proto);
  }
  return 0;
}
void jspSoftInit() {
  execInfo.root = jsvFindOrCreateRoot();
  execInfo.hiddenRoot = jsvObjectGetChild(execInfo.root, "\xFF", JSV_OBJECT);
  execInfo.execute = EXEC_YES;
  execInfo.scopesVar = 0;
  execInfo.baseScope = execInfo.root;
  execInfo.blockScope = 0;
  execInfo.blockCount = 0;
}
void jspSoftKill() {
  do { if (!(execInfo.baseScope==execInfo.root)) jsAssertFail("bin/espruino_embedded.c",11926,""); } while(0);
  do { if (!(execInfo.blockScope==0)) jsAssertFail("bin/espruino_embedded.c",11927,""); } while(0);
  do { if (!(execInfo.blockCount==0)) jsAssertFail("bin/espruino_embedded.c",11928,""); } while(0);
  jsvUnLock(execInfo.scopesVar);
  execInfo.scopesVar = 0;
  jsvUnLock(execInfo.hiddenRoot);
  execInfo.hiddenRoot = 0;
  jsvUnLock(execInfo.root);
  execInfo.root = 0;
}
void jspInit() {
  jspSoftInit();
}
void jspKill() {
  jspSoftKill();
  JsVar *r = jsvFindOrCreateRoot();
  jsvUnRef(r);
  jsvUnLock(r);
}
JsVar *jspEvaluateExpressionVar(JsVar *str) {
  JsLex lex;
  do { if (!(jsvIsString(str))) jsAssertFail("bin/espruino_embedded.c",11955,""); } while(0);
  JsLex *oldLex = jslSetLex(&lex);
  jslInit(str);
  JsVar *v = jspeExpression();
  jslKill();
  jslSetLex(oldLex);
  return jsvSkipNameAndUnLock(v);
}
JsVar *jspEvaluateVar(JsVar *str, JsVar *scope, const char *stackTraceName) {
  JsLex lex;
  do { if (!(jsvIsString(str))) jsAssertFail("bin/espruino_embedded.c",11970,""); } while(0);
  JsLex *oldLex = jslSetLex(&lex);
  jslInit(str);
  lex.lastLex = oldLex;
  lex.functionName = stackTraceName?jsvNewFromString(stackTraceName):0;
  JsExecInfo oldExecInfo = execInfo;
  execInfo.execute = EXEC_YES;
  if (scope) {
    execInfo.scopesVar = 0;
    if (scope!=execInfo.root) {
      jspeiAddScope(scope);
      execInfo.baseScope = scope;
    }
  }
  JsVar *v = jspParse();
  if (scope) jspeiClearScopes();
  jslKill();
  jsvUnLock(lex.functionName);
  jslSetLex(oldLex);
  oldExecInfo.execute |= execInfo.execute & EXEC_PERSIST;
  execInfo = oldExecInfo;
  return jsvSkipNameAndUnLock(v);
}
JsVar *jspEvaluate(const char *str, bool stringIsStatic) {
  JsVar *evCode;
  if (stringIsStatic)
    evCode = jsvNewNativeString((char*)str, strlen(str));
  else
    evCode = jsvNewFromString(str);
  if (!evCode) return 0;
  JsVar *v = 0;
  if (!jsvIsMemoryFull())
    v = jspEvaluateVar(evCode, 0, "[raw]");
  jsvUnLock(evCode);
  return v;
}
JsVar *jspExecuteJSFunctionCode(const char *argNames, const char *jsCode, size_t jsCodeLen, JsVar *thisArg, int argCount, JsVar **argPtr) {
  if (jsCodeLen==0) jsCodeLen = strlen(jsCode);
  JsVar *fn = jsvNewWithFlags(JSV_FUNCTION);
  if (!fn) return 0;
  if (argNames && *argNames) {
    char name[10];
    int nameLen;
    name[0] = (char)0xFF;
    while (*argNames) {
      const char *argEnd = argNames;
      nameLen = 1;
      while (*argEnd && *argEnd!=',') {
        name[nameLen++] = *argEnd;
        argEnd++;
      }
      name[nameLen]=0;
      JsVar *paramName = jsvNewNameFromString(name);
      jsvAddFunctionParameter(fn, paramName, 0);
      argNames = (*argEnd)?argEnd+1:argEnd;
    }
  }
  jsvObjectSetChildAndUnLock(fn, "\xFF""cod", jsvNewNativeString((char*)jsCode, jsCodeLen));
  JsVar *result = jspExecuteFunction(fn,thisArg,argCount,argPtr);
  jsvUnLock(fn);
  return result;
}
JsVar *jspExecuteFunction(JsVar *func, JsVar *thisArg, int argCount, JsVar **argPtr) {
  JsExecInfo oldExecInfo = execInfo;
  execInfo.scopesVar = 0;
  execInfo.execute = EXEC_YES;
  execInfo.thisVar = 0;
  JsVar *result = jspeFunctionCall(func, 0, thisArg, false, argCount, argPtr);
  jspeiClearScopes();
  oldExecInfo.execute |= execInfo.execute&EXEC_PERSIST;
  jspeiClearScopes();
  execInfo = oldExecInfo;
  return result;
}
JsVar *jspEvaluateModule(JsVar *moduleContents) {
  do { if (!(jsvIsString(moduleContents) || jsvIsFunction(moduleContents))) jsAssertFail("bin/espruino_embedded.c",12077,""); } while(0);
  if (jsvIsFunction(moduleContents)) {
    moduleContents = jsvObjectGetChildIfExists(moduleContents,"\xFF""cod");
    if (!jsvIsString(moduleContents)) {
      jsvUnLock(moduleContents);
      return 0;
    }
  } else
    jsvLockAgain(moduleContents);
  JsVar *scope = jsvNewObject();
  JsVar *scopeExports = jsvNewObject();
  if (!scope || !scopeExports) {
    jsvUnLock3(scope, scopeExports, moduleContents);
    return 0;
  }
  JsVar *exportsName = jsvAddNamedChild(scope, scopeExports, "exports");
  jsvUnLock2(scopeExports, jsvAddNamedChild(scope, scope, "module"));
  JsExecInfo oldExecInfo = execInfo;
  execInfo.baseScope = scopeExports;
  execInfo.blockScope = 0;
  execInfo.blockCount = 0;
  execInfo.thisVar = scopeExports;
  jsvUnLock(jspEvaluateVar(moduleContents, scope, "module"));
  do { if (!(execInfo.blockCount==0)) jsAssertFail("bin/espruino_embedded.c",12104,""); } while(0);
  do { if (!(execInfo.blockScope==0)) jsAssertFail("bin/espruino_embedded.c",12105,""); } while(0);
  JsExecFlags hasError = (execInfo.execute)&EXEC_ERROR_MASK;
  execInfo = oldExecInfo;
  execInfo.execute |= hasError;
  jsvUnLock2(moduleContents, scope);
  return jsvSkipNameAndUnLock(exportsName);
}
JsVar *jspGetPrototypeOwner(JsVar *proto) {
  if (jsvIsObject(proto) || jsvIsArray(proto)) {
    return jsvSkipNameAndUnLock(jsvObjectGetChildIfExists(proto, "constructor"));
  }
  return 0;
}
JsVar *jswrap_array_constructor(JsVar *args);
bool jswrap_array_contains(JsVar *parent, JsVar *value);
JsVar *jswrap_array_indexOf(JsVar *parent, JsVar *value, JsVarInt startIdx);
bool jswrap_array_includes(JsVar *parent, JsVar *value, JsVarInt startIdx);
JsVar *jswrap_array_join(JsVar *parent, JsVar *filler);
JsVarInt jswrap_array_push(JsVar *parent, JsVar *args);
JsVar *jswrap_array_map(JsVar *parent, JsVar *funcVar, JsVar *thisVar);
JsVar *jswrap_array_shift(JsVar *parent);
JsVarInt jswrap_array_unshift(JsVar *parent, JsVar *elements);
JsVar *jswrap_array_slice(JsVar *parent, JsVarInt start, JsVar *endVar);
JsVar *jswrap_array_splice(JsVar *parent, JsVarInt index, JsVar *howManyVar, JsVar *elements);
JsVar *jswrap_array_splice_i(JsVar *parent, JsVarInt index, JsVarInt howMany, JsVar *elements);
void jswrap_array_forEach(JsVar *parent, JsVar *funcVar, JsVar *thisVar);
JsVar *jswrap_array_filter(JsVar *parent, JsVar *funcVar, JsVar *thisVar);
JsVar *jswrap_array_find(JsVar *parent, JsVar *funcVar);
JsVar *jswrap_array_findIndex(JsVar *parent, JsVar *funcVar);
JsVar *jswrap_array_some(JsVar *parent, JsVar *funcVar, JsVar *thisVar);
JsVar *jswrap_array_every(JsVar *parent, JsVar *funcVar, JsVar *thisVar);
JsVar *jswrap_array_reduce(JsVar *parent, JsVar *funcVar, JsVar *initialValue);
JsVar *jswrap_array_sort (JsVar *array, JsVar *compareFn);
JsVar *jswrap_array_concat(JsVar *parent, JsVar *args);
JsVar *jswrap_array_fill(JsVar *parent, JsVar *value, JsVarInt start, JsVar *endVar);
JsVar *jswrap_array_reverse(JsVar *parent);
typedef struct {
  int daysSinceEpoch;
  int ms,sec,min,hour;
  int zone;
  bool is_dst;
} TimeInDay;
typedef struct {
  int daysSinceEpoch;
  int day;
  int month;
  int year;
  int dow;
} CalendarDate;
int getDayNumberFromDate(int y, int m, int d);
void getDateFromDayNumber(int day, int *y, int *m, int *date);
JsVarFloat getDstChangeTime(int y, int dow_number, int month, int dow, int day_offset, int timeOfDay, bool is_start, int dst_offset, int timezone, bool as_local_time);
int jstGetEffectiveTimeZone(JsVarFloat ms, bool is_local_time, bool *is_dst);
void setCorrectTimeZone(TimeInDay *td);
TimeInDay getTimeFromMilliSeconds(JsVarFloat ms_in, bool forceGMT);
JsVarFloat fromTimeInDay(TimeInDay *td);
CalendarDate getCalendarDate(int d);
int fromCalendarDate(CalendarDate *date);
JsVarFloat jswrap_date_now();
JsVar *jswrap_date_from_milliseconds(JsVarFloat time);
JsVar *jswrap_date_constructor(JsVar *args);
int jswrap_date_getTimezoneOffset(JsVar *parent);
JsVarFloat jswrap_date_getTime(JsVar *parent);
JsVarFloat jswrap_date_setTime(JsVar *date, JsVarFloat timeValue);
int jswrap_date_getHours(JsVar *parent);
int jswrap_date_getMinutes(JsVar *parent);
int jswrap_date_getSeconds(JsVar *parent);
int jswrap_date_getMilliseconds(JsVar *parent);
int jswrap_date_getDay(JsVar *parent);
int jswrap_date_getDate(JsVar *parent);
int jswrap_date_getMonth(JsVar *parent);
int jswrap_date_getFullYear(JsVar *parent);
int jswrap_date_getIsDST(JsVar *parent);
JsVarFloat jswrap_date_setHours(JsVar *parent, int hoursValue, JsVar *minutesValue, JsVar *secondsValue, JsVar *millisecondsValue);
JsVarFloat jswrap_date_setMinutes(JsVar *parent, int minutesValue, JsVar *secondsValue, JsVar *millisecondsValue);
JsVarFloat jswrap_date_setSeconds(JsVar *parent, int secondsValue, JsVar *millisecondsValue);
JsVarFloat jswrap_date_setMilliseconds(JsVar *parent, int millisecondsValue);
JsVarFloat jswrap_date_setDate(JsVar *parent, int dayValue);
JsVarFloat jswrap_date_setMonth(JsVar *parent, int monthValue, JsVar *dayValue);
JsVarFloat jswrap_date_setFullYear(JsVar *parent, int yearValue, JsVar *monthValue, JsVar *dayValue);
JsVar *jswrap_date_toString(JsVar *parent);
JsVar *jswrap_date_toUTCString(JsVar *parent);
JsVar *jswrap_date_toISOString(JsVar *parent);
JsVar *jswrap_date_toLocalISOString(JsVar *parent);
JsVarFloat jswrap_date_parse(JsVar *str);
JsVar *jswrap_number_constructor(JsVar *val);
JsVar *jswrap_number_toFixed(JsVar *parent, int decimals);
JsVar *jswrap_require(JsVar *modulename);
JsVar *jswrap_modules_getCached();
void jswrap_modules_removeCached(JsVar *id);
void jswrap_modules_removeAllCached();
void jswrap_modules_addCached(JsVar *id, JsVar *sourceCode);

JsVar *jswrap_heatshrink_compress(JsVar *data);
JsVar *jswrap_heatshrink_decompress(JsVar *data);
static JsVar* gen_jswrap_Array_pop(JsVar *parent) {
  return jsvSkipNameAndUnLock(jsvArrayPop(parent));
}
static bool gen_jswrap_Array_isArray(JsVar* var) {
  return jsvIsArray(var);
}
static JsVarInt gen_jswrap_ArrayBuffer_byteLength(JsVar *parent) {
  return (JsVarInt)(parent->varData.arraybuffer.length);
}
static JsVar* gen_jswrap_Uint8Array_Uint8Array(JsVar* arr, JsVarInt byteOffset, JsVarInt length) {
  return jswrap_typedarray_constructor(ARRAYBUFFERVIEW_UINT8, arr, byteOffset, length);
}
static JsVar* gen_jswrap_Uint8ClampedArray_Uint8ClampedArray(JsVar* arr, JsVarInt byteOffset, JsVarInt length) {
  return jswrap_typedarray_constructor(ARRAYBUFFERVIEW_UINT8|ARRAYBUFFERVIEW_CLAMPED, arr, byteOffset, length);
}
static JsVar* gen_jswrap_Int8Array_Int8Array(JsVar* arr, JsVarInt byteOffset, JsVarInt length) {
  return jswrap_typedarray_constructor(ARRAYBUFFERVIEW_INT8, arr, byteOffset, length);
}
static JsVar* gen_jswrap_Uint16Array_Uint16Array(JsVar* arr, JsVarInt byteOffset, JsVarInt length) {
  return jswrap_typedarray_constructor(ARRAYBUFFERVIEW_UINT16, arr, byteOffset, length);
}
static JsVar* gen_jswrap_Int16Array_Int16Array(JsVar* arr, JsVarInt byteOffset, JsVarInt length) {
  return jswrap_typedarray_constructor(ARRAYBUFFERVIEW_INT16, arr, byteOffset, length);
}
static JsVar* gen_jswrap_Uint24Array_Uint24Array(JsVar* arr, JsVarInt byteOffset, JsVarInt length) {
  return jswrap_typedarray_constructor(ARRAYBUFFERVIEW_UINT24, arr, byteOffset, length);
}
static JsVar* gen_jswrap_Uint32Array_Uint32Array(JsVar* arr, JsVarInt byteOffset, JsVarInt length) {
  return jswrap_typedarray_constructor(ARRAYBUFFERVIEW_UINT32, arr, byteOffset, length);
}
static JsVar* gen_jswrap_Int32Array_Int32Array(JsVar* arr, JsVarInt byteOffset, JsVarInt length) {
  return jswrap_typedarray_constructor(ARRAYBUFFERVIEW_INT32, arr, byteOffset, length);
}
static JsVar* gen_jswrap_Float32Array_Float32Array(JsVar* arr, JsVarInt byteOffset, JsVarInt length) {
  return jswrap_typedarray_constructor(ARRAYBUFFERVIEW_FLOAT32, arr, byteOffset, length);
}
static JsVar* gen_jswrap_Float64Array_Float64Array(JsVar* arr, JsVarInt byteOffset, JsVarInt length) {
  return jswrap_typedarray_constructor(ARRAYBUFFERVIEW_FLOAT64, arr, byteOffset, length);
}
static JsVar* gen_jswrap_ArrayBufferView_buffer(JsVar *parent) {
  return jsvLock(jsvGetFirstChild(parent));
}
static JsVarInt gen_jswrap_ArrayBufferView_byteLength(JsVar *parent) {
  return (JsVarInt)(parent->varData.arraybuffer.length * (size_t)((parent->varData.arraybuffer.type)&ARRAYBUFFERVIEW_MASK_SIZE));
}
static JsVarInt gen_jswrap_ArrayBufferView_byteOffset(JsVar *parent) {
  return parent->varData.arraybuffer.byteOffset;
}
static JsVar* gen_jswrap_DataView_getFloat32(JsVar *parent, JsVarInt byteOffset, bool littleEndian) {
  return jswrap_dataview_get(parent, ARRAYBUFFERVIEW_FLOAT32, byteOffset, littleEndian);
}
static JsVar* gen_jswrap_DataView_getFloat64(JsVar *parent, JsVarInt byteOffset, bool littleEndian) {
  return jswrap_dataview_get(parent, ARRAYBUFFERVIEW_FLOAT64, byteOffset, littleEndian);
}
static JsVar* gen_jswrap_DataView_getInt8(JsVar *parent, JsVarInt byteOffset, bool littleEndian) {
  return jswrap_dataview_get(parent, ARRAYBUFFERVIEW_INT8, byteOffset, littleEndian);
}
static JsVar* gen_jswrap_DataView_getInt16(JsVar *parent, JsVarInt byteOffset, bool littleEndian) {
  return jswrap_dataview_get(parent, ARRAYBUFFERVIEW_INT16, byteOffset, littleEndian);
}
static JsVar* gen_jswrap_DataView_getInt32(JsVar *parent, JsVarInt byteOffset, bool littleEndian) {
  return jswrap_dataview_get(parent, ARRAYBUFFERVIEW_INT32, byteOffset, littleEndian);
}
static JsVar* gen_jswrap_DataView_getUint8(JsVar *parent, JsVarInt byteOffset, bool littleEndian) {
  return jswrap_dataview_get(parent, ARRAYBUFFERVIEW_UINT8, byteOffset, littleEndian);
}
static JsVar* gen_jswrap_DataView_getUint16(JsVar *parent, JsVarInt byteOffset, bool littleEndian) {
  return jswrap_dataview_get(parent, ARRAYBUFFERVIEW_UINT16, byteOffset, littleEndian);
}
static JsVar* gen_jswrap_DataView_getUint32(JsVar *parent, JsVarInt byteOffset, bool littleEndian) {
  return jswrap_dataview_get(parent, ARRAYBUFFERVIEW_UINT32, byteOffset, littleEndian);
}
static void gen_jswrap_DataView_setFloat32(JsVar *parent, JsVarInt byteOffset, JsVar* value, bool littleEndian) {
  jswrap_dataview_set(parent, ARRAYBUFFERVIEW_FLOAT32, byteOffset, value, littleEndian);
}
static void gen_jswrap_DataView_setFloat64(JsVar *parent, JsVarInt byteOffset, JsVar* value, bool littleEndian) {
  jswrap_dataview_set(parent, ARRAYBUFFERVIEW_FLOAT64, byteOffset, value, littleEndian);
}
static void gen_jswrap_DataView_setInt8(JsVar *parent, JsVarInt byteOffset, JsVar* value, bool littleEndian) {
  jswrap_dataview_set(parent, ARRAYBUFFERVIEW_INT8, byteOffset, value, littleEndian);
}
static void gen_jswrap_DataView_setInt16(JsVar *parent, JsVarInt byteOffset, JsVar* value, bool littleEndian) {
  jswrap_dataview_set(parent, ARRAYBUFFERVIEW_INT16, byteOffset, value, littleEndian);
}
static void gen_jswrap_DataView_setInt32(JsVar *parent, JsVarInt byteOffset, JsVar* value, bool littleEndian) {
  jswrap_dataview_set(parent, ARRAYBUFFERVIEW_INT32, byteOffset, value, littleEndian);
}
static void gen_jswrap_DataView_setUint8(JsVar *parent, JsVarInt byteOffset, JsVar* value, bool littleEndian) {
  jswrap_dataview_set(parent, ARRAYBUFFERVIEW_UINT8, byteOffset, value, littleEndian);
}
static void gen_jswrap_DataView_setUint16(JsVar *parent, JsVarInt byteOffset, JsVar* value, bool littleEndian) {
  jswrap_dataview_set(parent, ARRAYBUFFERVIEW_UINT16, byteOffset, value, littleEndian);
}
static void gen_jswrap_DataView_setUint32(JsVar *parent, JsVarInt byteOffset, JsVar* value, bool littleEndian) {
  jswrap_dataview_set(parent, ARRAYBUFFERVIEW_UINT32, byteOffset, value, littleEndian);
}
static JsVarFloat gen_jswrap_NaN() {
  return NAN;
}
static JsVarFloat gen_jswrap_Infinity() {
  return INFINITY;
}
static JsVarFloat gen_jswrap_Number_NaN() {
  return NAN;
}
static JsVarFloat gen_jswrap_Number_MAX_VALUE() {
  return 1.7976931348623157e+308;
}
static JsVarFloat gen_jswrap_Number_MIN_VALUE() {
  return 2.2250738585072014e-308;
}
static JsVarFloat gen_jswrap_Number_NEGATIVE_INFINITY() {
  return -INFINITY;
}
static JsVarFloat gen_jswrap_Number_POSITIVE_INFINITY() {
  return INFINITY;
}
static int gen_jswrap_HIGH() {
  return 1;
}
static int gen_jswrap_LOW() {
  return 0;
}
static JsVar* gen_jswrap_Object_keys(JsVar* object) {
  return jswrap_object_keys_or_property_names(object, JSWOKPF_NONE);
}
static JsVar* gen_jswrap_Object_getOwnPropertyNames(JsVar* object) {
  return jswrap_object_keys_or_property_names(object, JSWOKPF_INCLUDE_NON_ENUMERABLE);
}
static JsVar* gen_jswrap_Object_values(JsVar* object) {
  return jswrap_object_values_or_entries(object, false);;
}
static JsVar* gen_jswrap_Object_entries(JsVar* object) {
  return jswrap_object_values_or_entries(object, true);;
}
static int gen_jswrap_String_indexOf(JsVar *parent, JsVar* substring, JsVar* fromIndex) {
  return jswrap_string_indexOf(parent, substring, fromIndex, false);
}
static int gen_jswrap_String_lastIndexOf(JsVar *parent, JsVar* substring, JsVar* fromIndex) {
  return jswrap_string_indexOf(parent, substring, fromIndex, true);
}
static JsVar* gen_jswrap_String_toLowerCase(JsVar *parent) {
  return jswrap_string_toUpperLowerCase(parent, false);
}
static JsVar* gen_jswrap_String_toUpperCase(JsVar *parent) {
  return jswrap_string_toUpperLowerCase(parent, true);
}
static JsVar* gen_jswrap_String_removeAccents(JsVar *parent) {
  return jswrap_string_removeAccents(parent);
}
static bool gen_jswrap_String_includes(JsVar *parent, JsVar* substring, JsVar* fromIndex) {
  return jswrap_string_indexOf(parent, substring, fromIndex, false)>=0;
}
static JsVar* gen_jswrap_String_padStart(JsVar *parent, JsVarInt targetLength, JsVar* padString) {
  return jswrap_string_padX(parent, targetLength, padString, true);
}
static JsVar* gen_jswrap_String_padEnd(JsVar *parent, JsVarInt targetLength, JsVar* padString) {
  return jswrap_string_padX(parent, targetLength, padString, false);
}
static JsVarFloat gen_jswrap_Math_E() {
  return 2.718281828459045;
}
static JsVarFloat gen_jswrap_Math_PI() {
  return (3.141592653589793);
}
static JsVarFloat gen_jswrap_Math_LN2() {
  return 0.6931471805599453;
}
static JsVarFloat gen_jswrap_Math_LN10() {
  return 2.302585092994046;
}
static JsVarFloat gen_jswrap_Math_LOG2E() {
  return 1.4426950408889634;
}
static JsVarFloat gen_jswrap_Math_LOG10E() {
  return 0.4342944819032518;
}
static JsVarFloat gen_jswrap_Math_SQRT2() {
  return 1.4142135623730951;
}
static JsVarFloat gen_jswrap_Math_SQRT1_2() {
  return 0.7071067811865476;
}
static JsVarFloat gen_jswrap_Math_acos(JsVarFloat x) {
  return ((3.141592653589793)/2) - jswrap_math_asin(x);
}
static JsVarFloat gen_jswrap_Math_random() {
  return (JsVarFloat)rand() / (JsVarFloat)((unsigned)RAND_MAX+1);
}
static JsVarInt gen_jswrap_Math_randInt(JsVarInt range) {
  return (range>0) ? (rand() % range) : (rand()^(rand()<<1));
}
static JsVarFloat gen_jswrap_Math_tan(JsVarFloat theta) {
  return jswrap_math_sin(theta) / jswrap_math_sin(theta+((3.141592653589793)/2));
}
static JsVarFloat gen_jswrap_Math_min(JsVar* args) {
  return jswrap_math_minmax(args, false);
}
static JsVarFloat gen_jswrap_Math_max(JsVar* args) {
  return jswrap_math_minmax(args, true);
}
static JsVar* gen_jswrap_ArrayBufferView_ArrayBufferView() {
  return NULL;
}
static JsVar* gen_jswrap_console_console() {
  return NULL;
}
static JsVar* gen_jswrap_JSON_JSON() {
  return NULL;
}
static JsVar* gen_jswrap_Modules_Modules() {
  return NULL;
}
static JsVar* gen_jswrap_Math_Math() {
  return NULL;
}
static JsVar* gen_jswrap_heatshrink_heatshrink() {
  return NULL;
}
JsVar *jswBinarySearch(const JswSymList *symbolsPtr, JsVar *parent, const char *name) {
  uint8_t symbolCount = (*(uint8_t*)(&symbolsPtr->symbolCount));
  int searchMin = 0;
  int searchMax = symbolCount - 1;
  while (searchMin <= searchMax) {
    int idx = (searchMin+searchMax) >> 1;
    const JswSymPtr *sym = &symbolsPtr->symbols[idx];
    int cmp = strcmp(name, &symbolsPtr->symbolChars[((sym)->strOffset)]);
    if (cmp==0) {
      unsigned short functionSpec = sym->functionSpec;
      if ((functionSpec & JSWAT_EXECUTE_IMMEDIATELY_MASK) == JSWAT_EXECUTE_IMMEDIATELY)
        return jsnCallFunction(((sym)->functionPtr), functionSpec, parent, 0, 0);
      return jsvNewNativeFunction(((sym)->functionPtr), functionSpec);
    } else {
      if (cmp<0) {
        searchMax = idx-1;
      } else {
        searchMin = idx+1;
      }
    }
  }
  return 0;
}
static const JswSymPtr jswSymbols_global[] = {
  { 0, JSWAT_JSVAR | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_array_constructor},
  { 6, JSWAT_JSVAR | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_arraybuffer_constructor},
  { 18, JSWAT_JSVAR, (void*)gen_jswrap_ArrayBufferView_ArrayBufferView},
  { 34, JSWAT_BOOL | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_boolean_constructor},
  { 42, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)jswrap_dataview_constructor},
  { 51, JSWAT_JSVAR | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_date_constructor},
  { 56, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_error_constructor},
  { 62, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)gen_jswrap_Float32Array_Float32Array},
  { 75, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)gen_jswrap_Float64Array_Float64Array},
  { 88, JSWAT_JSVAR | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_function_constructor},
  { 97, JSWAT_INT32 | JSWAT_EXECUTE_IMMEDIATELY, (void*)gen_jswrap_HIGH},
  { 102, JSWAT_JSVARFLOAT | JSWAT_EXECUTE_IMMEDIATELY, (void*)gen_jswrap_Infinity},
  { 111, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)gen_jswrap_Int16Array_Int16Array},
  { 122, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)gen_jswrap_Int32Array_Int32Array},
  { 133, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)gen_jswrap_Int8Array_Int8Array},
  { 143, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_internalerror_constructor},
  { 157, JSWAT_JSVAR, (void*)gen_jswrap_JSON_JSON},
  { 162, JSWAT_INT32 | JSWAT_EXECUTE_IMMEDIATELY, (void*)gen_jswrap_LOW},
  { 166, JSWAT_JSVAR, (void*)gen_jswrap_Math_Math},
  { 171, JSWAT_JSVAR, (void*)gen_jswrap_Modules_Modules},
  { 179, JSWAT_JSVARFLOAT | JSWAT_EXECUTE_IMMEDIATELY, (void*)gen_jswrap_NaN},
  { 183, JSWAT_JSVAR | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_number_constructor},
  { 190, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_object_constructor},
  { 197, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_referenceerror_constructor},
  { 212, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_regexp_constructor},
  { 219, JSWAT_JSVAR | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_string_constructor},
  { 226, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_syntaxerror_constructor},
  { 238, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_typeerror_constructor},
  { 248, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)gen_jswrap_Uint16Array_Uint16Array},
  { 260, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)gen_jswrap_Uint24Array_Uint24Array},
  { 272, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)gen_jswrap_Uint32Array_Uint32Array},
  { 284, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)gen_jswrap_Uint8Array_Uint8Array},
  { 295, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)gen_jswrap_Uint8ClampedArray_Uint8ClampedArray},
  { 313, JSWAT_JSVAR | JSWAT_EXECUTE_IMMEDIATELY, (void*)jswrap_arguments},
  { 323, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_atob},
  { 328, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_btoa},
  { 333, JSWAT_JSVAR, (void*)gen_jswrap_console_console},
  { 341, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_decodeURIComponent},
  { 360, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_encodeURIComponent},
  { 379, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_eval},
  { 384, JSWAT_JSVAR | JSWAT_EXECUTE_IMMEDIATELY, (void*)jswrap_global},
  { 391, JSWAT_JSVAR | JSWAT_EXECUTE_IMMEDIATELY, (void*)jswrap_global},
  { 402, JSWAT_BOOL | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_isFinite},
  { 411, JSWAT_BOOL | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_isNaN},
  { 417, JSWAT_JSVARFLOAT | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_parseFloat},
  { 428, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_parseInt},
  { 437, JSWAT_VOID | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_print},
  { 443, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_require},
  { 451, JSWAT_VOID | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_trace}
};
static const unsigned char jswSymbolIndex_global = 0;
static const JswSymPtr jswSymbols_Array_proto[] = {
  { 0, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_array_concat},
  { 7, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_array_every},
  { 13, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)jswrap_array_fill},
  { 18, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_array_filter},
  { 25, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_array_find},
  { 30, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_array_findIndex},
  { 40, JSWAT_VOID | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_array_forEach},
  { 48, JSWAT_BOOL | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_array_includes},
  { 57, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_array_indexOf},
  { 65, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_array_join},
  { 70, JSWAT_JSVAR | JSWAT_THIS_ARG | JSWAT_EXECUTE_IMMEDIATELY, (void*)jswrap_object_length},
  { 77, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_array_map},
  { 81, JSWAT_JSVAR | JSWAT_THIS_ARG, (void*)gen_jswrap_Array_pop},
  { 85, JSWAT_INT32 | JSWAT_THIS_ARG | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_array_push},
  { 90, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_array_reduce},
  { 97, JSWAT_JSVAR | JSWAT_THIS_ARG, (void*)jswrap_array_reverse},
  { 105, JSWAT_JSVAR | JSWAT_THIS_ARG, (void*)jswrap_array_shift},
  { 111, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_array_slice},
  { 117, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_array_some},
  { 122, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_array_sort},
  { 127, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)jswrap_array_splice},
  { 134, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_object_toString},
  { 143, JSWAT_INT32 | JSWAT_THIS_ARG | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_array_unshift}
};
static const unsigned char jswSymbolIndex_Array_proto = 1;
static const JswSymPtr jswSymbols_Array[] = {
  { 0, JSWAT_BOOL | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)gen_jswrap_Array_isArray}
};
static const unsigned char jswSymbolIndex_Array = 2;
static const JswSymPtr jswSymbols_ArrayBuffer_proto[] = {
  { 0, JSWAT_INT32 | JSWAT_THIS_ARG | JSWAT_EXECUTE_IMMEDIATELY, (void*)gen_jswrap_ArrayBuffer_byteLength}
};
static const unsigned char jswSymbolIndex_ArrayBuffer_proto = 3;
static const JswSymPtr jswSymbols_ArrayBufferView_proto[] = {
  { 0, JSWAT_JSVAR | JSWAT_THIS_ARG | JSWAT_EXECUTE_IMMEDIATELY, (void*)gen_jswrap_ArrayBufferView_buffer},
  { 7, JSWAT_INT32 | JSWAT_THIS_ARG | JSWAT_EXECUTE_IMMEDIATELY, (void*)gen_jswrap_ArrayBufferView_byteLength},
  { 18, JSWAT_INT32 | JSWAT_THIS_ARG | JSWAT_EXECUTE_IMMEDIATELY, (void*)gen_jswrap_ArrayBufferView_byteOffset},
  { 29, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)jswrap_array_fill},
  { 34, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_array_filter},
  { 41, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_array_find},
  { 46, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_array_findIndex},
  { 56, JSWAT_VOID | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_array_forEach},
  { 64, JSWAT_BOOL | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_array_includes},
  { 73, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_array_indexOf},
  { 81, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_array_join},
  { 86, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_arraybufferview_map},
  { 90, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_array_reduce},
  { 97, JSWAT_JSVAR | JSWAT_THIS_ARG, (void*)jswrap_array_reverse},
  { 105, JSWAT_VOID | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_arraybufferview_set},
  { 109, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_array_slice},
  { 115, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_arraybufferview_sort},
  { 120, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_arraybufferview_subarray}
};
static const unsigned char jswSymbolIndex_ArrayBufferView_proto = 4;
static const JswSymPtr jswSymbols_DataView_proto[] = {
  { 0, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_BOOL << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)gen_jswrap_DataView_getFloat32},
  { 11, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_BOOL << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)gen_jswrap_DataView_getFloat64},
  { 22, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_BOOL << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)gen_jswrap_DataView_getInt16},
  { 31, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_BOOL << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)gen_jswrap_DataView_getInt32},
  { 40, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_BOOL << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)gen_jswrap_DataView_getInt8},
  { 48, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_BOOL << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)gen_jswrap_DataView_getUint16},
  { 58, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_BOOL << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)gen_jswrap_DataView_getUint32},
  { 68, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_BOOL << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)gen_jswrap_DataView_getUint8},
  { 77, JSWAT_VOID | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_BOOL << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)gen_jswrap_DataView_setFloat32},
  { 88, JSWAT_VOID | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_BOOL << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)gen_jswrap_DataView_setFloat64},
  { 99, JSWAT_VOID | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_BOOL << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)gen_jswrap_DataView_setInt16},
  { 108, JSWAT_VOID | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_BOOL << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)gen_jswrap_DataView_setInt32},
  { 117, JSWAT_VOID | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_BOOL << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)gen_jswrap_DataView_setInt8},
  { 125, JSWAT_VOID | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_BOOL << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)gen_jswrap_DataView_setUint16},
  { 135, JSWAT_VOID | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_BOOL << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)gen_jswrap_DataView_setUint32},
  { 145, JSWAT_VOID | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_BOOL << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)gen_jswrap_DataView_setUint8}
};
static const unsigned char jswSymbolIndex_DataView_proto = 5;
static const JswSymPtr jswSymbols_Date[] = {
  { 0, JSWAT_JSVARFLOAT, (void*)jswrap_date_now},
  { 4, JSWAT_JSVARFLOAT | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_date_parse}
};
static const unsigned char jswSymbolIndex_Date = 6;
static const JswSymPtr jswSymbols_Date_proto[] = {
  { 0, JSWAT_INT32 | JSWAT_THIS_ARG, (void*)jswrap_date_getDate},
  { 8, JSWAT_INT32 | JSWAT_THIS_ARG, (void*)jswrap_date_getDay},
  { 15, JSWAT_INT32 | JSWAT_THIS_ARG, (void*)jswrap_date_getFullYear},
  { 27, JSWAT_INT32 | JSWAT_THIS_ARG, (void*)jswrap_date_getHours},
  { 36, JSWAT_INT32 | JSWAT_THIS_ARG, (void*)jswrap_date_getIsDST},
  { 45, JSWAT_INT32 | JSWAT_THIS_ARG, (void*)jswrap_date_getMilliseconds},
  { 61, JSWAT_INT32 | JSWAT_THIS_ARG, (void*)jswrap_date_getMinutes},
  { 72, JSWAT_INT32 | JSWAT_THIS_ARG, (void*)jswrap_date_getMonth},
  { 81, JSWAT_INT32 | JSWAT_THIS_ARG, (void*)jswrap_date_getSeconds},
  { 92, JSWAT_JSVARFLOAT | JSWAT_THIS_ARG, (void*)jswrap_date_getTime},
  { 100, JSWAT_INT32 | JSWAT_THIS_ARG, (void*)jswrap_date_getTimezoneOffset},
  { 118, JSWAT_JSVARFLOAT | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_date_setDate},
  { 126, JSWAT_JSVARFLOAT | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)jswrap_date_setFullYear},
  { 138, JSWAT_JSVARFLOAT | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*4)), (void*)jswrap_date_setHours},
  { 147, JSWAT_JSVARFLOAT | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_date_setMilliseconds},
  { 163, JSWAT_JSVARFLOAT | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)jswrap_date_setMinutes},
  { 174, JSWAT_JSVARFLOAT | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_date_setMonth},
  { 183, JSWAT_JSVARFLOAT | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_date_setSeconds},
  { 194, JSWAT_JSVARFLOAT | JSWAT_THIS_ARG | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_date_setTime},
  { 202, JSWAT_JSVAR | JSWAT_THIS_ARG, (void*)jswrap_date_toISOString},
  { 214, JSWAT_JSVAR | JSWAT_THIS_ARG, (void*)jswrap_date_toISOString},
  { 221, JSWAT_JSVAR | JSWAT_THIS_ARG, (void*)jswrap_date_toLocalISOString},
  { 238, JSWAT_JSVAR | JSWAT_THIS_ARG, (void*)jswrap_date_toString},
  { 247, JSWAT_JSVAR | JSWAT_THIS_ARG, (void*)jswrap_date_toUTCString},
  { 259, JSWAT_JSVARFLOAT | JSWAT_THIS_ARG, (void*)jswrap_date_getTime}
};
static const unsigned char jswSymbolIndex_Date_proto = 7;
static const JswSymPtr jswSymbols_Error_proto[] = {
  { 0, JSWAT_JSVAR | JSWAT_THIS_ARG, (void*)jswrap_error_toString}
};
static const unsigned char jswSymbolIndex_Error_proto = 8;
static const JswSymPtr jswSymbols_SyntaxError_proto[] = {
  { 0, JSWAT_JSVAR | JSWAT_THIS_ARG, (void*)jswrap_error_toString}
};
static const unsigned char jswSymbolIndex_SyntaxError_proto = 9;
static const JswSymPtr jswSymbols_TypeError_proto[] = {
  { 0, JSWAT_JSVAR | JSWAT_THIS_ARG, (void*)jswrap_error_toString}
};
static const unsigned char jswSymbolIndex_TypeError_proto = 10;
static const JswSymPtr jswSymbols_InternalError_proto[] = {
  { 0, JSWAT_JSVAR | JSWAT_THIS_ARG, (void*)jswrap_error_toString}
};
static const unsigned char jswSymbolIndex_InternalError_proto = 11;
static const JswSymPtr jswSymbols_ReferenceError_proto[] = {
  { 0, JSWAT_JSVAR | JSWAT_THIS_ARG, (void*)jswrap_error_toString}
};
static const unsigned char jswSymbolIndex_ReferenceError_proto = 12;
static const JswSymPtr jswSymbols_console[] = {
  { 0, JSWAT_VOID | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_print},
  { 6, JSWAT_VOID | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_print},
  { 12, JSWAT_VOID | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_print},
  { 17, JSWAT_VOID | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_print},
  { 21, JSWAT_VOID | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_console_trace},
  { 27, JSWAT_VOID | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_print}
};
static const unsigned char jswSymbolIndex_console = 13;
static const JswSymPtr jswSymbols_JSON[] = {
  { 0, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_json_parse},
  { 6, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)jswrap_json_stringify}
};
static const unsigned char jswSymbolIndex_JSON = 14;
static const JswSymPtr jswSymbols_Number[] = {
  { 0, JSWAT_JSVARFLOAT | JSWAT_EXECUTE_IMMEDIATELY, (void*)gen_jswrap_Number_MAX_VALUE},
  { 10, JSWAT_JSVARFLOAT | JSWAT_EXECUTE_IMMEDIATELY, (void*)gen_jswrap_Number_MIN_VALUE},
  { 20, JSWAT_JSVARFLOAT | JSWAT_EXECUTE_IMMEDIATELY, (void*)gen_jswrap_Number_NEGATIVE_INFINITY},
  { 38, JSWAT_JSVARFLOAT | JSWAT_EXECUTE_IMMEDIATELY, (void*)gen_jswrap_Number_NaN},
  { 42, JSWAT_JSVARFLOAT | JSWAT_EXECUTE_IMMEDIATELY, (void*)gen_jswrap_Number_POSITIVE_INFINITY}
};
static const unsigned char jswSymbolIndex_Number = 15;
static const JswSymPtr jswSymbols_Number_proto[] = {
  { 0, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_number_toFixed}
};
static const unsigned char jswSymbolIndex_Number_proto = 16;
static const JswSymPtr jswSymbols_Object_proto[] = {
  { 0, JSWAT_JSVAR | JSWAT_THIS_ARG, (void*)jswrap_object_clone},
  { 6, JSWAT_BOOL | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_object_hasOwnProperty},
  { 21, JSWAT_JSVAR | JSWAT_THIS_ARG | JSWAT_EXECUTE_IMMEDIATELY, (void*)jswrap_object_length},
  { 28, JSWAT_VOID | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_object_removeAllListeners},
  { 47, JSWAT_VOID | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_object_removeListener},
  { 62, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_object_toString},
  { 71, JSWAT_JSVAR | JSWAT_THIS_ARG, (void*)jswrap_object_valueOf}
};
static const unsigned char jswSymbolIndex_Object_proto = 17;
static const JswSymPtr jswSymbols_Object[] = {
  { 0, JSWAT_JSVAR | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_object_assign},
  { 7, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_object_create},
  { 14, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_object_defineProperties},
  { 31, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)jswrap_object_defineProperty},
  { 46, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)gen_jswrap_Object_entries},
  { 54, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_object_fromEntries},
  { 66, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_object_getOwnPropertyDescriptor},
  { 91, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_object_getOwnPropertyDescriptors},
  { 117, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)gen_jswrap_Object_getOwnPropertyNames},
  { 137, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_object_getPrototypeOf},
  { 152, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)gen_jswrap_Object_keys},
  { 157, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_object_setPrototypeOf},
  { 172, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)gen_jswrap_Object_values}
};
static const unsigned char jswSymbolIndex_Object = 18;
static const JswSymPtr jswSymbols_Function_proto[] = {
  { 0, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_function_apply_or_call},
  { 6, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_function_bind},
  { 11, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_function_apply_or_call},
  { 16, JSWAT_VOID | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_function_replaceWith}
};
static const unsigned char jswSymbolIndex_Function_proto = 19;
static const JswSymPtr jswSymbols_RegExp_proto[] = {
  { 0, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_regexp_exec},
  { 5, JSWAT_BOOL | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_regexp_test}
};
static const unsigned char jswSymbolIndex_RegExp_proto = 20;
static const JswSymPtr jswSymbols_String_proto[] = {
  { 0, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_string_charAt},
  { 7, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_string_charCodeAt},
  { 18, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_string_concat},
  { 25, JSWAT_BOOL | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_string_endsWith},
  { 34, JSWAT_BOOL | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)gen_jswrap_String_includes},
  { 43, JSWAT_INT32 | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)gen_jswrap_String_indexOf},
  { 51, JSWAT_INT32 | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)gen_jswrap_String_lastIndexOf},
  { 63, JSWAT_JSVAR | JSWAT_THIS_ARG | JSWAT_EXECUTE_IMMEDIATELY, (void*)jswrap_object_length},
  { 70, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_string_match},
  { 76, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)gen_jswrap_String_padEnd},
  { 83, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)gen_jswrap_String_padStart},
  { 92, JSWAT_JSVAR | JSWAT_THIS_ARG, (void*)gen_jswrap_String_removeAccents},
  { 106, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_string_repeat},
  { 113, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_string_replace},
  { 121, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_string_replaceAll},
  { 132, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_string_slice},
  { 138, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_string_split},
  { 144, JSWAT_BOOL | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_string_startsWith},
  { 155, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_string_substr},
  { 162, JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_string_substring},
  { 172, JSWAT_JSVAR | JSWAT_THIS_ARG, (void*)gen_jswrap_String_toLowerCase},
  { 184, JSWAT_JSVAR | JSWAT_THIS_ARG, (void*)gen_jswrap_String_toUpperCase},
  { 196, JSWAT_JSVAR | JSWAT_THIS_ARG, (void*)jswrap_string_trim}
};
static const unsigned char jswSymbolIndex_String_proto = 21;
static const JswSymPtr jswSymbols_String[] = {
  { 0, JSWAT_JSVAR | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_string_fromCharCode}
};
static const unsigned char jswSymbolIndex_String = 22;
static const JswSymPtr jswSymbols_Modules[] = {
  { 0, JSWAT_VOID | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_modules_addCached},
  { 10, JSWAT_JSVAR, (void*)jswrap_modules_getCached},
  { 20, JSWAT_VOID, (void*)jswrap_modules_removeAllCached},
  { 36, JSWAT_VOID | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_modules_removeCached}
};
static const unsigned char jswSymbolIndex_Modules = 23;
static const JswSymPtr jswSymbols_Math[] = {
  { 0, JSWAT_JSVARFLOAT | JSWAT_EXECUTE_IMMEDIATELY, (void*)gen_jswrap_Math_E},
  { 2, JSWAT_JSVARFLOAT | JSWAT_EXECUTE_IMMEDIATELY, (void*)gen_jswrap_Math_LN10},
  { 7, JSWAT_JSVARFLOAT | JSWAT_EXECUTE_IMMEDIATELY, (void*)gen_jswrap_Math_LN2},
  { 11, JSWAT_JSVARFLOAT | JSWAT_EXECUTE_IMMEDIATELY, (void*)gen_jswrap_Math_LOG10E},
  { 18, JSWAT_JSVARFLOAT | JSWAT_EXECUTE_IMMEDIATELY, (void*)gen_jswrap_Math_LOG2E},
  { 24, JSWAT_JSVARFLOAT | JSWAT_EXECUTE_IMMEDIATELY, (void*)gen_jswrap_Math_PI},
  { 27, JSWAT_JSVARFLOAT | JSWAT_EXECUTE_IMMEDIATELY, (void*)gen_jswrap_Math_SQRT1_2},
  { 35, JSWAT_JSVARFLOAT | JSWAT_EXECUTE_IMMEDIATELY, (void*)gen_jswrap_Math_SQRT2},
  { 41, JSWAT_JSVARFLOAT | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)fabs},
  { 45, JSWAT_JSVARFLOAT | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)gen_jswrap_Math_acos},
  { 50, JSWAT_JSVARFLOAT | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_math_asin},
  { 55, JSWAT_JSVARFLOAT | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_math_atan},
  { 60, JSWAT_JSVARFLOAT | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_math_atan2},
  { 66, JSWAT_JSVARFLOAT | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)ceil},
  { 71, JSWAT_JSVARFLOAT | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)), (void*)jswrap_math_clip},
  { 76, JSWAT_JSVARFLOAT | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_math_cos},
  { 80, JSWAT_JSVARFLOAT | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)exp},
  { 84, JSWAT_JSVARFLOAT | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)floor},
  { 90, JSWAT_JSVARFLOAT | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)log},
  { 94, JSWAT_JSVARFLOAT | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)gen_jswrap_Math_max},
  { 98, JSWAT_JSVARFLOAT | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)gen_jswrap_Math_min},
  { 102, JSWAT_JSVARFLOAT | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)jswrap_math_pow},
  { 106, JSWAT_INT32 | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)gen_jswrap_Math_randInt},
  { 114, JSWAT_JSVARFLOAT, (void*)gen_jswrap_Math_random},
  { 121, JSWAT_JSVAR | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_math_round},
  { 127, JSWAT_INT32 | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_math_sign},
  { 132, JSWAT_JSVARFLOAT | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_math_sin},
  { 136, JSWAT_JSVARFLOAT | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_math_sqrt},
  { 141, JSWAT_JSVARFLOAT | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)gen_jswrap_Math_tan},
  { 145, JSWAT_JSVARFLOAT | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)), (void*)wrapAround}
};
static const unsigned char jswSymbolIndex_Math = 24;
static const JswSymPtr jswSymbols_heatshrink[] = {
  { 0, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_heatshrink_compress},
  { 9, JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)), (void*)jswrap_heatshrink_decompress}
};
static const unsigned char jswSymbolIndex_heatshrink = 25;
static const char jswSymbols_global_str[] = "Array\0ArrayBuffer\0ArrayBufferView\0Boolean\0DataView\0Date\0Error\0Float32Array\0Float64Array\0Function\0HIGH\0Infinity\0Int16Array\0Int32Array\0Int8Array\0InternalError\0JSON\0LOW\0Math\0Modules\0NaN\0Number\0Object\0ReferenceError\0RegExp\0String\0SyntaxError\0TypeError\0Uint16Array\0Uint24Array\0Uint32Array\0Uint8Array\0Uint8ClampedArray\0arguments\0atob\0btoa\0console\0decodeURIComponent\0encodeURIComponent\0eval\0global\0globalThis\0isFinite\0isNaN\0parseFloat\0parseInt\0print\0require\0trace\0";
static const char jswSymbols_Array_proto_str[] = "concat\0every\0fill\0filter\0find\0findIndex\0forEach\0includes\0indexOf\0join\0length\0map\0pop\0push\0reduce\0reverse\0shift\0slice\0some\0sort\0splice\0toString\0unshift\0";
static const char jswSymbols_Array_str[] = "isArray\0";
static const char jswSymbols_ArrayBuffer_proto_str[] = "byteLength\0";
static const char jswSymbols_ArrayBufferView_proto_str[] = "buffer\0byteLength\0byteOffset\0fill\0filter\0find\0findIndex\0forEach\0includes\0indexOf\0join\0map\0reduce\0reverse\0set\0slice\0sort\0subarray\0";
static const char jswSymbols_DataView_proto_str[] = "getFloat32\0getFloat64\0getInt16\0getInt32\0getInt8\0getUint16\0getUint32\0getUint8\0setFloat32\0setFloat64\0setInt16\0setInt32\0setInt8\0setUint16\0setUint32\0setUint8\0";
static const char jswSymbols_Date_str[] = "now\0parse\0";
static const char jswSymbols_Date_proto_str[] = "getDate\0getDay\0getFullYear\0getHours\0getIsDST\0getMilliseconds\0getMinutes\0getMonth\0getSeconds\0getTime\0getTimezoneOffset\0setDate\0setFullYear\0setHours\0setMilliseconds\0setMinutes\0setMonth\0setSeconds\0setTime\0toISOString\0toJSON\0toLocalISOString\0toString\0toUTCString\0valueOf\0";
static const char jswSymbols_Error_proto_str[] = "toString\0";
static const char jswSymbols_SyntaxError_proto_str[] = "toString\0";
static const char jswSymbols_TypeError_proto_str[] = "toString\0";
static const char jswSymbols_InternalError_proto_str[] = "toString\0";
static const char jswSymbols_ReferenceError_proto_str[] = "toString\0";
static const char jswSymbols_console_str[] = "debug\0error\0info\0log\0trace\0warn\0";
static const char jswSymbols_JSON_str[] = "parse\0stringify\0";
static const char jswSymbols_Number_str[] = "MAX_VALUE\0MIN_VALUE\0NEGATIVE_INFINITY\0NaN\0POSITIVE_INFINITY\0";
static const char jswSymbols_Number_proto_str[] = "toFixed\0";
static const char jswSymbols_Object_proto_str[] = "clone\0hasOwnProperty\0length\0removeAllListeners\0removeListener\0toString\0valueOf\0";
static const char jswSymbols_Object_str[] = "assign\0create\0defineProperties\0defineProperty\0entries\0fromEntries\0getOwnPropertyDescriptor\0getOwnPropertyDescriptors\0getOwnPropertyNames\0getPrototypeOf\0keys\0setPrototypeOf\0values\0";
static const char jswSymbols_Function_proto_str[] = "apply\0bind\0call\0replaceWith\0";
static const char jswSymbols_RegExp_proto_str[] = "exec\0test\0";
static const char jswSymbols_String_proto_str[] = "charAt\0charCodeAt\0concat\0endsWith\0includes\0indexOf\0lastIndexOf\0length\0match\0padEnd\0padStart\0removeAccents\0repeat\0replace\0replaceAll\0slice\0split\0startsWith\0substr\0substring\0toLowerCase\0toUpperCase\0trim\0";
static const char jswSymbols_String_str[] = "fromCharCode\0";
static const char jswSymbols_Modules_str[] = "addCached\0getCached\0removeAllCached\0removeCached\0";
static const char jswSymbols_Math_str[] = "E\0LN10\0LN2\0LOG10E\0LOG2E\0PI\0SQRT1_2\0SQRT2\0abs\0acos\0asin\0atan\0atan2\0ceil\0clip\0cos\0exp\0floor\0log\0max\0min\0pow\0randInt\0random\0round\0sign\0sin\0sqrt\0tan\0wrap\0";
static const char jswSymbols_heatshrink_str[] = "compress\0decompress\0";
const JswSymList jswSymbolTables[] = {
  {jswSymbols_global, jswSymbols_global_str, 49},
  {jswSymbols_Array_proto, jswSymbols_Array_proto_str, 23},
  {jswSymbols_Array, jswSymbols_Array_str, 1},
  {jswSymbols_ArrayBuffer_proto, jswSymbols_ArrayBuffer_proto_str, 1},
  {jswSymbols_ArrayBufferView_proto, jswSymbols_ArrayBufferView_proto_str, 18},
  {jswSymbols_DataView_proto, jswSymbols_DataView_proto_str, 16},
  {jswSymbols_Date, jswSymbols_Date_str, 2},
  {jswSymbols_Date_proto, jswSymbols_Date_proto_str, 25},
  {jswSymbols_Error_proto, jswSymbols_Error_proto_str, 1},
  {jswSymbols_SyntaxError_proto, jswSymbols_SyntaxError_proto_str, 1},
  {jswSymbols_TypeError_proto, jswSymbols_TypeError_proto_str, 1},
  {jswSymbols_InternalError_proto, jswSymbols_InternalError_proto_str, 1},
  {jswSymbols_ReferenceError_proto, jswSymbols_ReferenceError_proto_str, 1},
  {jswSymbols_console, jswSymbols_console_str, 6},
  {jswSymbols_JSON, jswSymbols_JSON_str, 2},
  {jswSymbols_Number, jswSymbols_Number_str, 5},
  {jswSymbols_Number_proto, jswSymbols_Number_proto_str, 1},
  {jswSymbols_Object_proto, jswSymbols_Object_proto_str, 7},
  {jswSymbols_Object, jswSymbols_Object_str, 13},
  {jswSymbols_Function_proto, jswSymbols_Function_proto_str, 4},
  {jswSymbols_RegExp_proto, jswSymbols_RegExp_proto_str, 2},
  {jswSymbols_String_proto, jswSymbols_String_proto_str, 23},
  {jswSymbols_String, jswSymbols_String_str, 1},
  {jswSymbols_Modules, jswSymbols_Modules_str, 4},
  {jswSymbols_Math, jswSymbols_Math_str, 30},
  {jswSymbols_heatshrink, jswSymbols_heatshrink_str, 2},
};
const JswSymList *jswGetSymbolListForConstructorProto(JsVar *constructor) {
  void *constructorPtr = constructor->varData.native.ptr;
  if (constructorPtr==(void*)jswrap_dataview_constructor) return &jswSymbolTables[jswSymbolIndex_DataView_proto];
  if (constructorPtr==(void*)jswrap_date_constructor) return &jswSymbolTables[jswSymbolIndex_Date_proto];
  if (constructorPtr==(void*)jswrap_error_constructor) return &jswSymbolTables[jswSymbolIndex_Error_proto];
  if (constructorPtr==(void*)jswrap_syntaxerror_constructor) return &jswSymbolTables[jswSymbolIndex_SyntaxError_proto];
  if (constructorPtr==(void*)jswrap_typeerror_constructor) return &jswSymbolTables[jswSymbolIndex_TypeError_proto];
  if (constructorPtr==(void*)jswrap_internalerror_constructor) return &jswSymbolTables[jswSymbolIndex_InternalError_proto];
  if (constructorPtr==(void*)jswrap_referenceerror_constructor) return &jswSymbolTables[jswSymbolIndex_ReferenceError_proto];
  if (constructorPtr==(void*)jswrap_regexp_constructor) return &jswSymbolTables[jswSymbolIndex_RegExp_proto];
  return 0;
}
JsVar *jswFindBuiltInFunction(JsVar *parent, const char *name) {
  JsVar *v;
  if (parent && !jsvIsRoot(parent)) {
    if (jsvIsNativeFunction(parent)) {
      const JswSymList *l = jswGetSymbolListForObject(parent);
      if (l) {
        v = jswBinarySearch(l, parent, name);
        if (v) return v;
      }
    }
    if (jsvIsArray(parent)) {
      v = jswBinarySearch(&jswSymbolTables[jswSymbolIndex_Array_proto], parent, name);
      if (v) return v;
    }
    if (jsvIsArrayBuffer(parent) && parent->varData.arraybuffer.type==ARRAYBUFFERVIEW_ARRAYBUFFER) {
      v = jswBinarySearch(&jswSymbolTables[jswSymbolIndex_ArrayBuffer_proto], parent, name);
      if (v) return v;
    }
    if (jsvIsArrayBuffer(parent) && parent->varData.arraybuffer.type!=ARRAYBUFFERVIEW_ARRAYBUFFER) {
      v = jswBinarySearch(&jswSymbolTables[jswSymbolIndex_ArrayBufferView_proto], parent, name);
      if (v) return v;
    }
    if (jsvIsNumeric(parent)) {
      v = jswBinarySearch(&jswSymbolTables[jswSymbolIndex_Number_proto], parent, name);
      if (v) return v;
    }
    if (jsvIsFunction(parent)) {
      v = jswBinarySearch(&jswSymbolTables[jswSymbolIndex_Function_proto], parent, name);
      if (v) return v;
    }
    if (jsvIsString(parent)) {
      v = jswBinarySearch(&jswSymbolTables[jswSymbolIndex_String_proto], parent, name);
      if (v) return v;
    }
    JsVar *proto = jsvIsObject(parent)?jsvSkipNameAndUnLock(jsvFindChildFromString(parent, "__proto__")):0;
    JsVar *constructor = jsvIsObject(proto)?jsvSkipNameAndUnLock(jsvFindChildFromString(proto, "constructor")):0;
    jsvUnLock(proto);
    if (constructor && jsvIsNativeFunction(constructor)) {
      const JswSymList *l = jswGetSymbolListForConstructorProto(constructor);
      jsvUnLock(constructor);
      if (l) {
        v = jswBinarySearch(l, parent, name);
        if (v) return v;
      }
    } else {
      jsvUnLock(constructor);
    }
    v = jswBinarySearch(&jswSymbolTables[jswSymbolIndex_Object_proto], parent, name);
    if (v) return v;
  } else {
    return jswBinarySearch(&jswSymbolTables[jswSymbolIndex_global], parent, name);
  }
  return 0;
}
const JswSymList *jswGetSymbolListForObject(JsVar *parent) {
  if (jsvIsNativeFunction(parent)) {
    if ((void*)parent->varData.native.ptr==(void*)jswrap_array_constructor) return &jswSymbolTables[jswSymbolIndex_Array];
    if ((void*)parent->varData.native.ptr==(void*)jswrap_date_constructor) return &jswSymbolTables[jswSymbolIndex_Date];
    if ((void*)parent->varData.native.ptr==(void*)gen_jswrap_console_console) return &jswSymbolTables[jswSymbolIndex_console];
    if ((void*)parent->varData.native.ptr==(void*)gen_jswrap_JSON_JSON) return &jswSymbolTables[jswSymbolIndex_JSON];
    if ((void*)parent->varData.native.ptr==(void*)jswrap_number_constructor) return &jswSymbolTables[jswSymbolIndex_Number];
    if ((void*)parent->varData.native.ptr==(void*)jswrap_object_constructor) return &jswSymbolTables[jswSymbolIndex_Object];
    if ((void*)parent->varData.native.ptr==(void*)jswrap_string_constructor) return &jswSymbolTables[jswSymbolIndex_String];
    if ((void*)parent->varData.native.ptr==(void*)gen_jswrap_Modules_Modules) return &jswSymbolTables[jswSymbolIndex_Modules];
    if ((void*)parent->varData.native.ptr==(void*)gen_jswrap_Math_Math) return &jswSymbolTables[jswSymbolIndex_Math];
    if ((void*)parent->varData.native.ptr==(void*)gen_jswrap_heatshrink_heatshrink) return &jswSymbolTables[jswSymbolIndex_heatshrink];
  }
  if (parent==execInfo.root) return &jswSymbolTables[jswSymbolIndex_global];
  return 0;
}
const JswSymList *jswGetSymbolListForObjectProto(JsVar *parent) {
  if (jsvIsNativeFunction(parent)) {
    if ((void*)parent->varData.native.ptr==(void*)jswrap_array_constructor) return &jswSymbolTables[jswSymbolIndex_Array_proto];
    if ((void*)parent->varData.native.ptr==(void*)jswrap_arraybuffer_constructor) return &jswSymbolTables[jswSymbolIndex_ArrayBuffer_proto];
    if ((void*)parent->varData.native.ptr==(void*)gen_jswrap_ArrayBufferView_ArrayBufferView) return &jswSymbolTables[jswSymbolIndex_ArrayBufferView_proto];
    if ((void*)parent->varData.native.ptr==(void*)jswrap_number_constructor) return &jswSymbolTables[jswSymbolIndex_Number_proto];
    if ((void*)parent->varData.native.ptr==(void*)jswrap_function_constructor) return &jswSymbolTables[jswSymbolIndex_Function_proto];
    if ((void*)parent->varData.native.ptr==(void*)jswrap_string_constructor) return &jswSymbolTables[jswSymbolIndex_String_proto];
  }
  JsVar *constructor = jsvIsObject(parent)?jsvSkipNameAndUnLock(jsvFindChildFromString(parent, "constructor")):0;
  if (constructor && jsvIsNativeFunction(constructor)) {
    const JswSymList *l = jswGetSymbolListForConstructorProto(constructor);
    jsvUnLock(constructor);
    if (l) return l;
  }
  if (jsvIsArray(parent)) return &jswSymbolTables[jswSymbolIndex_Array_proto];
  if (jsvIsArrayBuffer(parent) && parent->varData.arraybuffer.type==ARRAYBUFFERVIEW_ARRAYBUFFER) return &jswSymbolTables[jswSymbolIndex_ArrayBuffer_proto];
  if (jsvIsArrayBuffer(parent) && parent->varData.arraybuffer.type!=ARRAYBUFFERVIEW_ARRAYBUFFER) return &jswSymbolTables[jswSymbolIndex_ArrayBufferView_proto];
  if (jsvIsNumeric(parent)) return &jswSymbolTables[jswSymbolIndex_Number_proto];
  if (jsvIsFunction(parent)) return &jswSymbolTables[jswSymbolIndex_Function_proto];
  if (jsvIsString(parent)) return &jswSymbolTables[jswSymbolIndex_String_proto];
  return &jswSymbolTables[jswSymbolIndex_Object_proto];
}
bool jswIsBuiltInObject(const char *name) {
  const char *objNames = "Array\0ArrayBuffer\0ArrayBufferView\0Uint8Array\0Uint8ClampedArray\0Int8Array\0Uint16Array\0Int16Array\0Uint24Array\0Uint32Array\0Int32Array\0Float32Array\0Float64Array\0DataView\0Date\0Error\0SyntaxError\0TypeError\0InternalError\0ReferenceError\0Function\0console\0JSON\0Number\0Object\0Boolean\0RegExp\0String\0Modules\0Math\0";
  const char *s = objNames;
  while (*s) {
    if (strcmp(s, name)==0) return true;
    s+=strlen(s)+1;
  }
  return false;
}
void *jswGetBuiltInLibrary(const char *name) {
  if (strcmp(name, "heatshrink")==0) return (void*)gen_jswrap_heatshrink_heatshrink;
  return 0;
}
const char *jswGetBasicObjectName(JsVar *var) {
  if (jsvIsArrayBuffer(var)) {
    if (var->varData.arraybuffer.type==ARRAYBUFFERVIEW_ARRAYBUFFER) return "ArrayBuffer";
    if (var->varData.arraybuffer.type==ARRAYBUFFERVIEW_UINT8) return "Uint8Array";
    if (var->varData.arraybuffer.type==(ARRAYBUFFERVIEW_UINT8|ARRAYBUFFERVIEW_CLAMPED)) return "Uint8ClampedArray";
    if (var->varData.arraybuffer.type==ARRAYBUFFERVIEW_INT8) return "Int8Array";
    if (var->varData.arraybuffer.type==ARRAYBUFFERVIEW_UINT16) return "Uint16Array";
    if (var->varData.arraybuffer.type==ARRAYBUFFERVIEW_INT16) return "Int16Array";
    if (var->varData.arraybuffer.type==ARRAYBUFFERVIEW_UINT24) return "Uint24Array";
    if (var->varData.arraybuffer.type==ARRAYBUFFERVIEW_UINT32) return "Uint32Array";
    if (var->varData.arraybuffer.type==ARRAYBUFFERVIEW_INT32) return "Int32Array";
    if (var->varData.arraybuffer.type==ARRAYBUFFERVIEW_FLOAT32) return "Float32Array";
    if (var->varData.arraybuffer.type==ARRAYBUFFERVIEW_FLOAT64) return "Float64Array";
  }
  if (jsvIsArray(var)) return "Array";
  if (jsvIsNumeric(var)) return "Number";
  if (jsvIsObject(var)) return "Object";
  if (jsvIsFunction(var)) return "Function";
  if (jsvIsString(var)) return "String";
  return 0;
}
const char *jswGetBasicObjectPrototypeName(const char *objectName) {
  if (!strcmp(objectName, "Uint8Array")) return "ArrayBufferView";
  if (!strcmp(objectName, "Uint8ClampedArray")) return "ArrayBufferView";
  if (!strcmp(objectName, "Int8Array")) return "ArrayBufferView";
  if (!strcmp(objectName, "Uint16Array")) return "ArrayBufferView";
  if (!strcmp(objectName, "Int16Array")) return "ArrayBufferView";
  if (!strcmp(objectName, "Uint24Array")) return "ArrayBufferView";
  if (!strcmp(objectName, "Uint32Array")) return "ArrayBufferView";
  if (!strcmp(objectName, "Int32Array")) return "ArrayBufferView";
  if (!strcmp(objectName, "Float32Array")) return "ArrayBufferView";
  if (!strcmp(objectName, "Float64Array")) return "ArrayBufferView";
  return strcmp(objectName,"Object") ? "Object" : 0;
}
bool jswIdle() {
  bool wasBusy = false;
  return wasBusy;
}
void jswHWInit() {
}
void jswInit() {
}
void jswKill() {
}
void jswGetPowerUsage(JsVar *devices) {
}
bool jswOnCharEvent(IOEventFlags channel, char charData) {
  ( (void)(channel) );
  ( (void)(charData) );
  return false;
}
void jswOnCustomEvent(IOEventFlags eventFlags, uint8_t *data, int dataLen) {
  ( (void)(eventFlags) );
  ( (void)(data) );
  ( (void)(dataLen) );
}
const char *jswGetBuiltInJSLibrary(const char *name) {
  ( (void)(name) );
  return 0;
}
const char *jswGetBuiltInLibraryNames() {
  return "heatshrink";
}
JsVar *jswCallFunctionHack(void *function, JsnArgumentType argumentSpecifier, JsVar *thisParam, JsVar **paramData, int paramCount) {
  switch((int)argumentSpecifier) {
    case JSWAT_VOID: {
      JsVar *result = 0;
      ((void(*)())function)();
      return result;
    }
    case JSWAT_VOID | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)): {
      JsVar *result = 0;
      ((void(*)(JsVar*,JsVar*))function)(thisParam,((paramCount>0)?paramData[0]:0));
      return result;
    }
    case JSWAT_VOID | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)): {
      JsVar *result = 0;
      ((void(*)(JsVar*,JsVar*,JsVar*))function)(thisParam,((paramCount>0)?paramData[0]:0),((paramCount>1)?paramData[1]:0));
      return result;
    }
    case JSWAT_VOID | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)): {
      JsVar *result = 0;
      ((void(*)(JsVar*,JsVar*,JsVar*,JsVar*))function)(thisParam,((paramCount>0)?paramData[0]:0),((paramCount>1)?paramData[1]:0),((paramCount>2)?paramData[2]:0));
      return result;
    }
    case JSWAT_VOID | JSWAT_THIS_ARG | (JSWAT_BOOL << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)): {
      JsVar *result = 0;
      ((void(*)(JsVar*,bool))function)(thisParam,jsvGetBool((paramCount>0)?paramData[0]:0));
      return result;
    }
    case JSWAT_JSVAR | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)): {
      JsVar *result = 0;
      result = (((JsVar*(*)(JsVarInt,JsVarInt))function)(jsvGetInteger((paramCount>0)?paramData[0]:0),jsvGetInteger((paramCount>1)?paramData[1]:0)));
      return result;
    }
    case JSWAT_JSVARFLOAT | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)): {
      JsVar *result = 0;
      result = jsvNewFromFloat(((JsVarFloat(*)(JsVarFloat,JsVarFloat))function)(jsvGetFloat((paramCount>0)?paramData[0]:0),jsvGetFloat((paramCount>1)?paramData[1]:0)));
      return result;
    }
    case JSWAT_INT32 | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)): {
      JsVar *result = 0;
      result = jsvNewFromInteger(((JsVarInt(*)(JsVarInt,JsVarInt))function)(jsvGetInteger((paramCount>0)?paramData[0]:0),jsvGetInteger((paramCount>1)?paramData[1]:0)));
      return result;
    }
    case JSWAT_JSVAR | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)): {
      JsVar *result = 0;
      JsVar *argArray = (paramCount>0)?jsvNewArray(&paramData[0],paramCount-0):jsvNewEmptyArray();
      result = (((JsVar*(*)(JsVar*))function)(argArray));
      jsvUnLock(argArray);
      return result;
    }
    case JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)): {
      JsVar *result = 0;
      result = (((JsVar*(*)(JsVar*,JsVar*))function)(thisParam,((paramCount>0)?paramData[0]:0)));
      return result;
    }
    case JSWAT_JSVAR | JSWAT_THIS_ARG | JSWAT_EXECUTE_IMMEDIATELY: {
      JsVar *result = 0;
      result = (((JsVar*(*)(JsVar*))function)(thisParam));
      return result;
    }
    case JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)): {
      JsVar *result = 0;
      result = (((JsVar*(*)(JsVar*,JsVar*,JsVarInt))function)(thisParam,((paramCount>0)?paramData[0]:0),jsvGetInteger((paramCount>1)?paramData[1]:0)));
      return result;
    }
    case JSWAT_BOOL | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)): {
      JsVar *result = 0;
      result = jsvNewFromBool(((bool(*)(JsVar*,JsVar*,JsVarInt))function)(thisParam,((paramCount>0)?paramData[0]:0),jsvGetInteger((paramCount>1)?paramData[1]:0)));
      return result;
    }
    case JSWAT_INT32 | JSWAT_THIS_ARG | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)): {
      JsVar *result = 0;
      JsVar *argArray = (paramCount>0)?jsvNewArray(&paramData[0],paramCount-0):jsvNewEmptyArray();
      result = jsvNewFromInteger(((JsVarInt(*)(JsVar*,JsVar*))function)(thisParam,argArray));
      jsvUnLock(argArray);
      return result;
    }
    case JSWAT_JSVAR | JSWAT_THIS_ARG: {
      JsVar *result = 0;
      result = (((JsVar*(*)(JsVar*))function)(thisParam));
      return result;
    }
    case JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)): {
      JsVar *result = 0;
      result = (((JsVar*(*)(JsVar*,JsVar*,JsVar*))function)(thisParam,((paramCount>0)?paramData[0]:0),((paramCount>1)?paramData[1]:0)));
      return result;
    }
    case JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)): {
      JsVar *result = 0;
      JsVar *argArray = (paramCount>2)?jsvNewArray(&paramData[2],paramCount-2):jsvNewEmptyArray();
      result = (((JsVar*(*)(JsVar*,JsVarInt,JsVar*,JsVar*))function)(thisParam,jsvGetInteger((paramCount>0)?paramData[0]:0),((paramCount>1)?paramData[1]:0),argArray));
      jsvUnLock(argArray);
      return result;
    }
    case JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)): {
      JsVar *result = 0;
      result = (((JsVar*(*)(JsVar*,JsVarInt,JsVar*))function)(thisParam,jsvGetInteger((paramCount>0)?paramData[0]:0),((paramCount>1)?paramData[1]:0)));
      return result;
    }
    case JSWAT_BOOL | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)): {
      JsVar *result = 0;
      result = jsvNewFromBool(((bool(*)(JsVar*))function)(((paramCount>0)?paramData[0]:0)));
      return result;
    }
    case JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)): {
      JsVar *result = 0;
      JsVar *argArray = (paramCount>0)?jsvNewArray(&paramData[0],paramCount-0):jsvNewEmptyArray();
      result = (((JsVar*(*)(JsVar*,JsVar*))function)(thisParam,argArray));
      jsvUnLock(argArray);
      return result;
    }
    case JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)): {
      JsVar *result = 0;
      result = (((JsVar*(*)(JsVar*,JsVar*,JsVarInt,JsVar*))function)(thisParam,((paramCount>0)?paramData[0]:0),jsvGetInteger((paramCount>1)?paramData[1]:0),((paramCount>2)?paramData[2]:0)));
      return result;
    }
    case JSWAT_JSVAR | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)): {
      JsVar *result = 0;
      result = (((JsVar*(*)(JsVarInt))function)(jsvGetInteger((paramCount>0)?paramData[0]:0)));
      return result;
    }
    case JSWAT_INT32 | JSWAT_THIS_ARG | JSWAT_EXECUTE_IMMEDIATELY: {
      JsVar *result = 0;
      result = jsvNewFromInteger(((JsVarInt(*)(JsVar*))function)(thisParam));
      return result;
    }
    case JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)): {
      JsVar *result = 0;
      result = (((JsVar*(*)(JsVar*,JsVarInt,JsVarInt))function)(((paramCount>0)?paramData[0]:0),jsvGetInteger((paramCount>1)?paramData[1]:0),jsvGetInteger((paramCount>2)?paramData[2]:0)));
      return result;
    }
    case JSWAT_VOID | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)): {
      JsVar *result = 0;
      ((void(*)(JsVar*,JsVar*,int))function)(thisParam,((paramCount>0)?paramData[0]:0),jsvGetInteger((paramCount>1)?paramData[1]:0));
      return result;
    }
    case JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_BOOL << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)): {
      JsVar *result = 0;
      result = (((JsVar*(*)(JsVar*,JsVarInt,bool))function)(thisParam,jsvGetInteger((paramCount>0)?paramData[0]:0),jsvGetBool((paramCount>1)?paramData[1]:0)));
      return result;
    }
    case JSWAT_VOID | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_BOOL << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)): {
      JsVar *result = 0;
      ((void(*)(JsVar*,JsVarInt,JsVar*,bool))function)(thisParam,jsvGetInteger((paramCount>0)?paramData[0]:0),((paramCount>1)?paramData[1]:0),jsvGetBool((paramCount>2)?paramData[2]:0));
      return result;
    }
    case JSWAT_JSVARFLOAT: {
      JsVar *result = 0;
      result = jsvNewFromFloat(((JsVarFloat(*)())function)());
      return result;
    }
    case JSWAT_INT32 | JSWAT_THIS_ARG: {
      JsVar *result = 0;
      result = jsvNewFromInteger(((int(*)(JsVar*))function)(thisParam));
      return result;
    }
    case JSWAT_JSVARFLOAT | JSWAT_THIS_ARG: {
      JsVar *result = 0;
      result = jsvNewFromFloat(((JsVarFloat(*)(JsVar*))function)(thisParam));
      return result;
    }
    case JSWAT_JSVARFLOAT | JSWAT_THIS_ARG | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)): {
      JsVar *result = 0;
      result = jsvNewFromFloat(((JsVarFloat(*)(JsVar*,JsVarFloat))function)(thisParam,jsvGetFloat((paramCount>0)?paramData[0]:0)));
      return result;
    }
    case JSWAT_JSVARFLOAT | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*4)): {
      JsVar *result = 0;
      result = jsvNewFromFloat(((JsVarFloat(*)(JsVar*,JsVarInt,JsVar*,JsVar*,JsVar*))function)(thisParam,jsvGetInteger((paramCount>0)?paramData[0]:0),((paramCount>1)?paramData[1]:0),((paramCount>2)?paramData[2]:0),((paramCount>3)?paramData[3]:0)));
      return result;
    }
    case JSWAT_JSVARFLOAT | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)): {
      JsVar *result = 0;
      result = jsvNewFromFloat(((JsVarFloat(*)(JsVar*,JsVarInt,JsVar*,JsVar*))function)(thisParam,jsvGetInteger((paramCount>0)?paramData[0]:0),((paramCount>1)?paramData[1]:0),((paramCount>2)?paramData[2]:0)));
      return result;
    }
    case JSWAT_JSVARFLOAT | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)): {
      JsVar *result = 0;
      result = jsvNewFromFloat(((JsVarFloat(*)(JsVar*,JsVarInt,JsVar*))function)(thisParam,jsvGetInteger((paramCount>0)?paramData[0]:0),((paramCount>1)?paramData[1]:0)));
      return result;
    }
    case JSWAT_JSVARFLOAT | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)): {
      JsVar *result = 0;
      result = jsvNewFromFloat(((JsVarFloat(*)(JsVar*,JsVarInt))function)(thisParam,jsvGetInteger((paramCount>0)?paramData[0]:0)));
      return result;
    }
    case JSWAT_JSVARFLOAT | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)): {
      JsVar *result = 0;
      result = jsvNewFromFloat(((JsVarFloat(*)(JsVar*))function)(((paramCount>0)?paramData[0]:0)));
      return result;
    }
    case JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)): {
      JsVar *result = 0;
      result = (((JsVar*(*)(JsVar*))function)(((paramCount>0)?paramData[0]:0)));
      return result;
    }
    case JSWAT_JSVAR | JSWAT_EXECUTE_IMMEDIATELY: {
      JsVar *result = 0;
      result = (((JsVar*(*)())function)());
      return result;
    }
    case JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)): {
      JsVar *result = 0;
      result = (((JsVar*(*)(JsVar*,JsVar*))function)(((paramCount>0)?paramData[0]:0),((paramCount>1)?paramData[1]:0)));
      return result;
    }
    case JSWAT_VOID | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)): {
      JsVar *result = 0;
      ((void(*)(JsVar*))function)(((paramCount>0)?paramData[0]:0));
      return result;
    }
    case JSWAT_VOID | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)): {
      JsVar *result = 0;
      JsVar *argArray = (paramCount>0)?jsvNewArray(&paramData[0],paramCount-0):jsvNewEmptyArray();
      ((void(*)(JsVar*))function)(argArray);
      jsvUnLock(argArray);
      return result;
    }
    case JSWAT_JSVAR | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)): {
      JsVar *result = 0;
      result = (((JsVar*(*)(JsVar*,JsVar*,JsVar*))function)(((paramCount>0)?paramData[0]:0),((paramCount>1)?paramData[1]:0),((paramCount>2)?paramData[2]:0)));
      return result;
    }
    case JSWAT_JSVARFLOAT | JSWAT_EXECUTE_IMMEDIATELY: {
      JsVar *result = 0;
      result = jsvNewFromFloat(((JsVarFloat(*)())function)());
      return result;
    }
    case JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)): {
      JsVar *result = 0;
      result = (((JsVar*(*)(JsVar*,int))function)(thisParam,jsvGetInteger((paramCount>0)?paramData[0]:0)));
      return result;
    }
    case JSWAT_INT32 | JSWAT_EXECUTE_IMMEDIATELY: {
      JsVar *result = 0;
      result = jsvNewFromInteger(((int(*)())function)());
      return result;
    }
    case JSWAT_BOOL | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)): {
      JsVar *result = 0;
      result = jsvNewFromBool(((bool(*)(JsVar*,JsVar*))function)(thisParam,((paramCount>0)?paramData[0]:0)));
      return result;
    }
    case JSWAT_JSVAR | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)): {
      JsVar *result = 0;
      JsVar *argArray = (paramCount>1)?jsvNewArray(&paramData[1],paramCount-1):jsvNewEmptyArray();
      result = (((JsVar*(*)(JsVar*,JsVar*,JsVar*))function)(thisParam,((paramCount>0)?paramData[0]:0),argArray));
      jsvUnLock(argArray);
      return result;
    }
    case JSWAT_INT32 | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)): {
      JsVar *result = 0;
      result = jsvNewFromInteger(((int(*)(JsVar*,JsVar*,JsVar*))function)(thisParam,((paramCount>0)?paramData[0]:0),((paramCount>1)?paramData[1]:0)));
      return result;
    }
    case JSWAT_BOOL | JSWAT_THIS_ARG | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)): {
      JsVar *result = 0;
      result = jsvNewFromBool(((bool(*)(JsVar*,JsVar*,JsVar*))function)(thisParam,((paramCount>0)?paramData[0]:0),((paramCount>1)?paramData[1]:0)));
      return result;
    }
    case JSWAT_JSVAR: {
      JsVar *result = 0;
      result = (((JsVar*(*)())function)());
      return result;
    }
    case JSWAT_VOID | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVAR << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)): {
      JsVar *result = 0;
      ((void(*)(JsVar*,JsVar*))function)(((paramCount>0)?paramData[0]:0),((paramCount>1)?paramData[1]:0));
      return result;
    }
    case JSWAT_JSVARFLOAT | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)): {
      JsVar *result = 0;
      result = jsvNewFromFloat(((JsVarFloat(*)(JsVarFloat))function)(jsvGetFloat((paramCount>0)?paramData[0]:0)));
      return result;
    }
    case JSWAT_INT32 | (JSWAT_INT32 << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)): {
      JsVar *result = 0;
      result = jsvNewFromInteger(((JsVarInt(*)(JsVarInt))function)(jsvGetInteger((paramCount>0)?paramData[0]:0)));
      return result;
    }
    case JSWAT_JSVAR | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)): {
      JsVar *result = 0;
      result = (((JsVar*(*)(JsVarFloat))function)(jsvGetFloat((paramCount>0)?paramData[0]:0)));
      return result;
    }
    case JSWAT_JSVARFLOAT | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)) | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)) | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*3)): {
      JsVar *result = 0;
      result = jsvNewFromFloat(((JsVarFloat(*)(JsVarFloat,JsVarFloat,JsVarFloat))function)(jsvGetFloat((paramCount>0)?paramData[0]:0),jsvGetFloat((paramCount>1)?paramData[1]:0),jsvGetFloat((paramCount>2)?paramData[2]:0)));
      return result;
    }
    case JSWAT_JSVARFLOAT | (JSWAT_ARGUMENT_ARRAY << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)): {
      JsVar *result = 0;
      JsVar *argArray = (paramCount>0)?jsvNewArray(&paramData[0],paramCount-0):jsvNewEmptyArray();
      result = jsvNewFromFloat(((JsVarFloat(*)(JsVar*))function)(argArray));
      jsvUnLock(argArray);
      return result;
    }
    case JSWAT_INT32 | (JSWAT_JSVARFLOAT << ((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*1)): {
      JsVar *result = 0;
      result = jsvNewFromInteger(((JsVarInt(*)(JsVarFloat))function)(jsvGetFloat((paramCount>0)?paramData[0]:0)));
      return result;
    }
  default: jsExceptionHere(JSET_ERROR,"Unknown argspec %d",argumentSpecifier);
  }
  return 0;
}

typedef enum {
    HSER_SINK_OK,
    HSER_SINK_ERROR_NULL=-1,
    HSER_SINK_ERROR_MISUSE=-2,
} HSE_sink_res;
typedef enum {
    HSER_POLL_EMPTY,
    HSER_POLL_MORE,
    HSER_POLL_ERROR_NULL=-1,
    HSER_POLL_ERROR_MISUSE=-2,
} HSE_poll_res;
typedef enum {
    HSER_FINISH_DONE,
    HSER_FINISH_MORE,
    HSER_FINISH_ERROR_NULL=-1,
} HSE_finish_res;
struct hs_index {
    uint16_t size;
    int16_t index[2 << 8];
};
typedef struct {
    uint16_t input_size;
    uint16_t match_scan_index;
    uint16_t match_length;
    uint16_t match_pos;
    uint16_t outgoing_bits;
    uint8_t outgoing_bits_count;
    uint8_t flags;
    uint8_t state;
    uint8_t current_byte;
    uint8_t bit_index;
    uint8_t buffer[2 << (8)];
} heatshrink_encoder;
void heatshrink_encoder_reset(heatshrink_encoder *hse);
HSE_sink_res heatshrink_encoder_sink(heatshrink_encoder *hse,
    uint8_t *in_buf, size_t size, size_t *input_size);
HSE_poll_res heatshrink_encoder_poll(heatshrink_encoder *hse,
    uint8_t *out_buf, size_t out_buf_size, size_t *output_size);
HSE_finish_res heatshrink_encoder_finish(heatshrink_encoder *hse);

typedef enum {
    HSES_NOT_FULL,
    HSES_FILLED,
    HSES_SEARCH,
    HSES_YIELD_TAG_BIT,
    HSES_YIELD_LITERAL,
    HSES_YIELD_BR_INDEX,
    HSES_YIELD_BR_LENGTH,
    HSES_SAVE_BACKLOG,
    HSES_FLUSH_BITS,
    HSES_DONE,
} HSE_state;
enum {
    FLAG_IS_FINISHING = 0x01,
};
typedef struct {
    uint8_t *buf;
    size_t buf_size;
    size_t *output_size;
} encoder_output_info;
static uint16_t get_input_offset(heatshrink_encoder *hse);
static uint16_t get_input_buffer_size(heatshrink_encoder *hse);
static uint16_t get_lookahead_size(heatshrink_encoder *hse);
static void add_tag_bit(heatshrink_encoder *hse, encoder_output_info *oi, uint8_t tag);
static int can_take_byte(encoder_output_info *oi);
static int is_finishing(heatshrink_encoder *hse);
static void save_backlog(heatshrink_encoder *hse);
static void push_bits(heatshrink_encoder *hse, uint8_t count, uint8_t bits,
    encoder_output_info *oi);
static uint8_t push_outgoing_bits(heatshrink_encoder *hse, encoder_output_info *oi);
static void push_literal_byte(heatshrink_encoder *hse, encoder_output_info *oi);
void heatshrink_encoder_reset(heatshrink_encoder *hse) {
    size_t buf_sz = (2 << (8));
    memset(hse->buffer, 0, buf_sz);
    hse->input_size = 0;
    hse->state = HSES_NOT_FULL;
    hse->match_scan_index = 0;
    hse->flags = 0;
    hse->bit_index = 0x80;
    hse->current_byte = 0x00;
    hse->match_length = 0;
    hse->outgoing_bits = 0x0000;
    hse->outgoing_bits_count = 0;
}
HSE_sink_res heatshrink_encoder_sink(heatshrink_encoder *hse,
        uint8_t *in_buf, size_t size, size_t *input_size) {
    if ((hse == NULL) || (in_buf == NULL) || (input_size == NULL)) {
        return HSER_SINK_ERROR_NULL;
    }
    if (is_finishing(hse)) { return HSER_SINK_ERROR_MISUSE; }
    if (hse->state != HSES_NOT_FULL) { return HSER_SINK_ERROR_MISUSE; }
    uint16_t write_offset = get_input_offset(hse) + hse->input_size;
    uint16_t ibs = get_input_buffer_size(hse);
    uint16_t rem = ibs - hse->input_size;
    uint16_t cp_sz = rem < size ? rem : size;
    memcpy(&hse->buffer[write_offset], in_buf, cp_sz);
    *input_size = cp_sz;
    hse->input_size += cp_sz;
                                                   ;
    if (cp_sz == rem) {
                                               ;
        hse->state = HSES_FILLED;
    }
    return HSER_SINK_OK;
}
static uint16_t find_longest_match(heatshrink_encoder *hse, uint16_t start,
    uint16_t end, const uint16_t maxlen, uint16_t *match_length);
static void do_indexing(heatshrink_encoder *hse);
static HSE_state st_step_search(heatshrink_encoder *hse);
static HSE_state st_yield_tag_bit(heatshrink_encoder *hse,
    encoder_output_info *oi);
static HSE_state st_e_yield_literal(heatshrink_encoder *hse,
    encoder_output_info *oi);
static HSE_state st_yield_br_index(heatshrink_encoder *hse,
    encoder_output_info *oi);
static HSE_state st_yield_br_length(heatshrink_encoder *hse,
    encoder_output_info *oi);
static HSE_state st_save_backlog(heatshrink_encoder *hse);
static HSE_state st_flush_bit_buffer(heatshrink_encoder *hse,
    encoder_output_info *oi);
HSE_poll_res heatshrink_encoder_poll(heatshrink_encoder *hse,
        uint8_t *out_buf, size_t out_buf_size, size_t *output_size) {
    if ((hse == NULL) || (out_buf == NULL) || (output_size == NULL)) {
        return HSER_POLL_ERROR_NULL;
    }
    if (out_buf_size == 0) {
                                                   ;
        return HSER_POLL_ERROR_MISUSE;
    }
    *output_size = 0;
    encoder_output_info oi;
    oi.buf = out_buf;
    oi.buf_size = out_buf_size;
    oi.output_size = output_size;
    while (1) {
                                                            ;
        uint8_t in_state = hse->state;
        switch (in_state) {
        case HSES_NOT_FULL:
            return HSER_POLL_EMPTY;
        case HSES_FILLED:
            do_indexing(hse);
            hse->state = HSES_SEARCH;
            break;
        case HSES_SEARCH:
            hse->state = st_step_search(hse);
            break;
        case HSES_YIELD_TAG_BIT:
            hse->state = st_yield_tag_bit(hse, &oi);
            break;
        case HSES_YIELD_LITERAL:
            hse->state = st_e_yield_literal(hse, &oi);
            break;
        case HSES_YIELD_BR_INDEX:
            hse->state = st_yield_br_index(hse, &oi);
            break;
        case HSES_YIELD_BR_LENGTH:
            hse->state = st_yield_br_length(hse, &oi);
            break;
        case HSES_SAVE_BACKLOG:
            hse->state = st_save_backlog(hse);
            break;
        case HSES_FLUSH_BITS:
            hse->state = st_flush_bit_buffer(hse, &oi);
            return HSER_POLL_EMPTY;
        case HSES_DONE:
            return HSER_POLL_EMPTY;
        default:
                                                             ;
            return HSER_POLL_ERROR_MISUSE;
        }
        if (hse->state == in_state) {
            if (*output_size == out_buf_size) return HSER_POLL_MORE;
        }
    }
}
HSE_finish_res heatshrink_encoder_finish(heatshrink_encoder *hse) {
    if (hse == NULL) { return HSER_FINISH_ERROR_NULL; }
                                         ;
    hse->flags |= FLAG_IS_FINISHING;
    if (hse->state == HSES_NOT_FULL) { hse->state = HSES_FILLED; }
    return hse->state == HSES_DONE ? HSER_FINISH_DONE : HSER_FINISH_MORE;
}
static HSE_state st_step_search(heatshrink_encoder *hse) {
    uint16_t window_length = get_input_buffer_size(hse);
    uint16_t lookahead_sz = get_lookahead_size(hse);
    uint16_t msi = hse->match_scan_index;
                                                                     ;
    bool fin = is_finishing(hse);
    if (msi > hse->input_size - (fin ? 1 : lookahead_sz)) {
                                           ;
        return fin ? HSES_FLUSH_BITS : HSES_SAVE_BACKLOG;
    }
    uint16_t input_offset = get_input_offset(hse);
    uint16_t end = input_offset + msi;
    uint16_t start = end - window_length;
    uint16_t max_possible = lookahead_sz;
    if (hse->input_size - msi < lookahead_sz) {
        max_possible = hse->input_size - msi;
    }
    uint16_t match_length = 0;
    uint16_t match_pos = find_longest_match(hse,
        start, end, max_possible, &match_length);
    if (match_pos == ((uint16_t)-1)) {
                                   ;
        hse->match_scan_index++;
        hse->match_length = 0;
        return HSES_YIELD_TAG_BIT;
    } else {
                                                                          ;
        hse->match_pos = match_pos;
        hse->match_length = match_length;
                                                                                       ;
        return HSES_YIELD_TAG_BIT;
    }
}
static HSE_state st_yield_tag_bit(heatshrink_encoder *hse,
        encoder_output_info *oi) {
    if (can_take_byte(oi)) {
        if (hse->match_length == 0) {
            add_tag_bit(hse, oi, 0x01);
            return HSES_YIELD_LITERAL;
        } else {
            add_tag_bit(hse, oi, 0x00);
            hse->outgoing_bits = hse->match_pos - 1;
            hse->outgoing_bits_count = (8);
            return HSES_YIELD_BR_INDEX;
        }
    } else {
        return HSES_YIELD_TAG_BIT;
    }
}
static HSE_state st_e_yield_literal(heatshrink_encoder *hse,
        encoder_output_info *oi) {
    if (can_take_byte(oi)) {
        push_literal_byte(hse, oi);
        return HSES_SEARCH;
    } else {
        return HSES_YIELD_LITERAL;
    }
}
static HSE_state st_yield_br_index(heatshrink_encoder *hse,
        encoder_output_info *oi) {
    if (can_take_byte(oi)) {
                                                             ;
        if (push_outgoing_bits(hse, oi) > 0) {
            return HSES_YIELD_BR_INDEX;
        } else {
            hse->outgoing_bits = hse->match_length - 1;
            hse->outgoing_bits_count = (6);
            return HSES_YIELD_BR_LENGTH;
        }
    } else {
        return HSES_YIELD_BR_INDEX;
    }
}
static HSE_state st_yield_br_length(heatshrink_encoder *hse,
        encoder_output_info *oi) {
    if (can_take_byte(oi)) {
                                                                 ;
        if (push_outgoing_bits(hse, oi) > 0) {
            return HSES_YIELD_BR_LENGTH;
        } else {
            hse->match_scan_index += hse->match_length;
            hse->match_length = 0;
            return HSES_SEARCH;
        }
    } else {
        return HSES_YIELD_BR_LENGTH;
    }
}
static HSE_state st_save_backlog(heatshrink_encoder *hse) {
                              ;
    save_backlog(hse);
    return HSES_NOT_FULL;
}
static HSE_state st_flush_bit_buffer(heatshrink_encoder *hse,
        encoder_output_info *oi) {
    if (hse->bit_index == 0x80) {
                         ;
        return HSES_DONE;
    } else if (can_take_byte(oi)) {
                                                                                 ;
        oi->buf[(*oi->output_size)++] = hse->current_byte;
                         ;
        return HSES_DONE;
    } else {
        return HSES_FLUSH_BITS;
    }
}
static void add_tag_bit(heatshrink_encoder *hse, encoder_output_info *oi, uint8_t tag) {
                                       ;
    push_bits(hse, 1, tag, oi);
}
static uint16_t get_input_offset(heatshrink_encoder *hse) {
    return get_input_buffer_size(hse);
}
static uint16_t get_input_buffer_size(heatshrink_encoder *hse) {
    return (1 << (8));
    (void)hse;
}
static uint16_t get_lookahead_size(heatshrink_encoder *hse) {
    return (1 << (6));
    (void)hse;
}
static void do_indexing(heatshrink_encoder *hse) {
    (void)hse;
}
static int is_finishing(heatshrink_encoder *hse) {
    return hse->flags & FLAG_IS_FINISHING;
}
static int can_take_byte(encoder_output_info *oi) {
    return *oi->output_size < oi->buf_size;
}
static uint16_t find_longest_match(heatshrink_encoder *hse, uint16_t start,
        uint16_t end, const uint16_t maxlen, uint16_t *match_length) {
                                                           ;
    uint8_t *buf = hse->buffer;
    uint16_t match_maxlen = 0;
    uint16_t match_index = ((uint16_t)-1);
    uint16_t len = 0;
    uint8_t * const needlepoint = &buf[end];
    int16_t pos;
    for (pos=end - 1; pos - (int16_t)start >= 0; pos--) {
        uint8_t * const pospoint = &buf[pos];
        if ((pospoint[match_maxlen] == needlepoint[match_maxlen])
            && (*pospoint == *needlepoint)) {
            for (len=1; len<maxlen; len++) {
                if (0) {
                                                                          ;
                }
                if (pospoint[len] != needlepoint[len]) { break; }
            }
            if (len > match_maxlen) {
                match_maxlen = len;
                match_index = pos;
                if (len == maxlen) { break; }
            }
        }
    }
    const size_t break_even_point =
      (1 + (8) +
          (6));
    if (match_maxlen > (break_even_point / 8)) {
                                            ;
        *match_length = match_maxlen;
        return end - match_index;
    }
                          ;
    return ((uint16_t)-1);
}
static uint8_t push_outgoing_bits(heatshrink_encoder *hse, encoder_output_info *oi) {
    uint8_t count = 0;
    uint8_t bits = 0;
    if (hse->outgoing_bits_count > 8) {
        count = 8;
        bits = hse->outgoing_bits >> (hse->outgoing_bits_count - 8);
    } else {
        count = hse->outgoing_bits_count;
        bits = hse->outgoing_bits;
    }
    if (count > 0) {
                                                                 ;
        push_bits(hse, count, bits, oi);
        hse->outgoing_bits_count -= count;
    }
    return count;
}
static void push_bits(heatshrink_encoder *hse, uint8_t count, uint8_t bits,
        encoder_output_info *oi) {
                      ;
                                                                ;
    if (count == 8 && hse->bit_index == 0x80) {
        oi->buf[(*oi->output_size)++] = bits;
    } else {
        int i;
        for (i=count - 1; i>=0; i--) {
            bool bit = bits & (1 << i);
            if (bit) { hse->current_byte |= hse->bit_index; }
            if (0) {
                                                                   ;
            }
            hse->bit_index >>= 1;
            if (hse->bit_index == 0x00) {
                hse->bit_index = 0x80;
                                                                  ;
                oi->buf[(*oi->output_size)++] = hse->current_byte;
                hse->current_byte = 0x00;
            }
        }
    }
}
static void push_literal_byte(heatshrink_encoder *hse, encoder_output_info *oi) {
    uint16_t processed_offset = hse->match_scan_index - 1;
    uint16_t input_offset = get_input_offset(hse) + processed_offset;
    uint8_t c = hse->buffer[input_offset];
                                              ;
    push_bits(hse, 8, c, oi);
}
static void save_backlog(heatshrink_encoder *hse) {
    size_t input_buf_sz = get_input_buffer_size(hse);
    uint16_t msi = hse->match_scan_index;
    uint16_t rem = input_buf_sz - msi;
    uint16_t shift_sz = input_buf_sz + rem;
    memmove(&hse->buffer[0],
        &hse->buffer[input_buf_sz - rem],
        shift_sz);
    hse->match_scan_index = 0;
    hse->input_size -= input_buf_sz - rem;
}
typedef enum {
    HSDR_SINK_OK,
    HSDR_SINK_FULL,
    HSDR_SINK_ERROR_NULL=-1,
} HSD_sink_res;
typedef enum {
    HSDR_POLL_EMPTY,
    HSDR_POLL_MORE,
    HSDR_POLL_ERROR_NULL=-1,
    HSDR_POLL_ERROR_UNKNOWN=-2,
} HSD_poll_res;
typedef enum {
    HSDR_FINISH_DONE,
    HSDR_FINISH_MORE,
    HSDR_FINISH_ERROR_NULL=-1,
} HSD_finish_res;
typedef struct {
    uint16_t input_size;
    uint16_t input_index;
    uint16_t output_count;
    uint16_t output_index;
    uint16_t head_index;
    uint8_t state;
    uint8_t current_byte;
    uint8_t bit_index;
    uint8_t buffers[(1 << (8))
        + 32];
} heatshrink_decoder;
void heatshrink_decoder_reset(heatshrink_decoder *hsd);
HSD_sink_res heatshrink_decoder_sink(heatshrink_decoder *hsd,
    uint8_t *in_buf, size_t size, size_t *input_size);
HSD_poll_res heatshrink_decoder_poll(heatshrink_decoder *hsd,
    uint8_t *out_buf, size_t out_buf_size, size_t *output_size);
HSD_finish_res heatshrink_decoder_finish(heatshrink_decoder *hsd);
typedef enum {
    HSDS_TAG_BIT,
    HSDS_YIELD_LITERAL,
    HSDS_BACKREF_INDEX_MSB,
    HSDS_BACKREF_INDEX_LSB,
    HSDS_BACKREF_COUNT_MSB,
    HSDS_BACKREF_COUNT_LSB,
    HSDS_YIELD_BACKREF,
} HSD_state;
typedef struct {
    uint8_t *buf;
    size_t buf_size;
    size_t *output_size;
} decoder_output_info;
static uint16_t get_bits(heatshrink_decoder *hsd, uint8_t count);
static void push_byte(heatshrink_decoder *hsd, decoder_output_info *oi, uint8_t byte);
void heatshrink_decoder_reset(heatshrink_decoder *hsd) {
    size_t buf_sz = 1 << (8);
    size_t input_sz = 32;
    memset(hsd->buffers, 0, buf_sz + input_sz);
    hsd->state = HSDS_TAG_BIT;
    hsd->input_size = 0;
    hsd->input_index = 0;
    hsd->bit_index = 0x00;
    hsd->current_byte = 0x00;
    hsd->output_count = 0;
    hsd->output_index = 0;
    hsd->head_index = 0;
}
HSD_sink_res heatshrink_decoder_sink(heatshrink_decoder *hsd,
        uint8_t *in_buf, size_t size, size_t *input_size) {
    if ((hsd == NULL) || (in_buf == NULL) || (input_size == NULL)) {
        return HSDR_SINK_ERROR_NULL;
    }
    size_t rem = 32 - hsd->input_size;
    if (rem == 0) {
        *input_size = 0;
        return HSDR_SINK_FULL;
    }
    size = rem < size ? rem : size;
                                       ;
    memcpy(&hsd->buffers[hsd->input_size], in_buf, size);
    hsd->input_size += size;
    *input_size = size;
    return HSDR_SINK_OK;
}
static HSD_state st_tag_bit(heatshrink_decoder *hsd);
static HSD_state st_d_yield_literal(heatshrink_decoder *hsd,
    decoder_output_info *oi);
static HSD_state st_backref_index_msb(heatshrink_decoder *hsd);
static HSD_state st_backref_index_lsb(heatshrink_decoder *hsd);
static HSD_state st_backref_count_msb(heatshrink_decoder *hsd);
static HSD_state st_backref_count_lsb(heatshrink_decoder *hsd);
static HSD_state st_yield_backref(heatshrink_decoder *hsd,
    decoder_output_info *oi);
HSD_poll_res heatshrink_decoder_poll(heatshrink_decoder *hsd,
        uint8_t *out_buf, size_t out_buf_size, size_t *output_size) {
    if ((hsd == NULL) || (out_buf == NULL) || (output_size == NULL)) {
        return HSDR_POLL_ERROR_NULL;
    }
    *output_size = 0;
    decoder_output_info oi;
    oi.buf = out_buf;
    oi.buf_size = out_buf_size;
    oi.output_size = output_size;
    while (1) {
                                                                 ;
        uint8_t in_state = hsd->state;
        switch (in_state) {
        case HSDS_TAG_BIT:
            hsd->state = st_tag_bit(hsd);
            break;
        case HSDS_YIELD_LITERAL:
            hsd->state = st_d_yield_literal(hsd, &oi);
            break;
        case HSDS_BACKREF_INDEX_MSB:
            hsd->state = st_backref_index_msb(hsd);
            break;
        case HSDS_BACKREF_INDEX_LSB:
            hsd->state = st_backref_index_lsb(hsd);
            break;
        case HSDS_BACKREF_COUNT_MSB:
            hsd->state = st_backref_count_msb(hsd);
            break;
        case HSDS_BACKREF_COUNT_LSB:
            hsd->state = st_backref_count_lsb(hsd);
            break;
        case HSDS_YIELD_BACKREF:
            hsd->state = st_yield_backref(hsd, &oi);
            break;
        default:
            return HSDR_POLL_ERROR_UNKNOWN;
        }
        if (hsd->state == in_state) {
            if (*output_size == out_buf_size) { return HSDR_POLL_MORE; }
            return HSDR_POLL_EMPTY;
        }
    }
}
static HSD_state st_tag_bit(heatshrink_decoder *hsd) {
    uint32_t bits = get_bits(hsd, 1);
    if (bits == ((uint16_t)-1)) {
        return HSDS_TAG_BIT;
    } else if (bits) {
        return HSDS_YIELD_LITERAL;
    } else if ((8) > 8) {
        return HSDS_BACKREF_INDEX_MSB;
    } else {
        hsd->output_index = 0;
        return HSDS_BACKREF_INDEX_LSB;
    }
}
static HSD_state st_d_yield_literal(heatshrink_decoder *hsd,
        decoder_output_info *oi) {
    if (*oi->output_size < oi->buf_size) {
        uint16_t byte = get_bits(hsd, 8);
        if (byte == ((uint16_t)-1)) { return HSDS_YIELD_LITERAL; }
        uint8_t *buf = &hsd->buffers[32];
        uint16_t mask = (1 << (8)) - 1;
        uint8_t c = byte & 0xFF;
                                                                                ;
        buf[hsd->head_index++ & mask] = c;
        push_byte(hsd, oi, c);
        return HSDS_TAG_BIT;
    } else {
        return HSDS_YIELD_LITERAL;
    }
}
static HSD_state st_backref_index_msb(heatshrink_decoder *hsd) {
    uint8_t bit_ct = ((8));
                      ;
    uint16_t bits = get_bits(hsd, bit_ct - 8);
                                                          ;
    if (bits == ((uint16_t)-1)) { return HSDS_BACKREF_INDEX_MSB; }
    hsd->output_index = bits << 8;
    return HSDS_BACKREF_INDEX_LSB;
}
static HSD_state st_backref_index_lsb(heatshrink_decoder *hsd) {
    uint8_t bit_ct = ((8));
    uint16_t bits = get_bits(hsd, bit_ct < 8 ? bit_ct : 8);
                                                          ;
    if (bits == ((uint16_t)-1)) { return HSDS_BACKREF_INDEX_LSB; }
    hsd->output_index |= bits;
    hsd->output_index++;
    uint8_t br_bit_ct = ((6));
    hsd->output_count = 0;
    return (br_bit_ct > 8) ? HSDS_BACKREF_COUNT_MSB : HSDS_BACKREF_COUNT_LSB;
}
static HSD_state st_backref_count_msb(heatshrink_decoder *hsd) {
    uint8_t br_bit_ct = ((6));
                         ;
    uint16_t bits = get_bits(hsd, br_bit_ct - 8);
                                                          ;
    if (bits == ((uint16_t)-1)) { return HSDS_BACKREF_COUNT_MSB; }
    hsd->output_count = bits << 8;
    return HSDS_BACKREF_COUNT_LSB;
}
static HSD_state st_backref_count_lsb(heatshrink_decoder *hsd) {
    uint8_t br_bit_ct = ((6));
    uint16_t bits = get_bits(hsd, br_bit_ct < 8 ? br_bit_ct : 8);
                                                          ;
    if (bits == ((uint16_t)-1)) { return HSDS_BACKREF_COUNT_LSB; }
    hsd->output_count |= bits;
    hsd->output_count++;
    return HSDS_YIELD_BACKREF;
}
static HSD_state st_yield_backref(heatshrink_decoder *hsd,
        decoder_output_info *oi) {
    size_t count = oi->buf_size - *oi->output_size;
    if (count > 0) {
        size_t i = 0;
        if (hsd->output_count < count) count = hsd->output_count;
        uint8_t *buf = &hsd->buffers[32];
        uint16_t mask = (1 << (8)) - 1;
        uint16_t neg_offset = hsd->output_index;
                                                                             ;
                                      ;
                                                               ;
        for (i=0; i<count; i++) {
            uint8_t c = buf[(hsd->head_index - neg_offset) & mask];
            push_byte(hsd, oi, c);
            buf[hsd->head_index & mask] = c;
            hsd->head_index++;
                                      ;
        }
        hsd->output_count -= count;
        if (hsd->output_count == 0) { return HSDS_TAG_BIT; }
    }
    return HSDS_YIELD_BACKREF;
}
static uint16_t get_bits(heatshrink_decoder *hsd, uint8_t count) {
    uint16_t accumulator = 0;
    int i = 0;
    if (count > 15) { return ((uint16_t)-1); }
                                        ;
    if (hsd->input_size == 0) {
        if (hsd->bit_index < (1 << (count - 1))) { return ((uint16_t)-1); }
    }
    for (i = 0; i < count; i++) {
        if (hsd->bit_index == 0x00) {
            if (hsd->input_size == 0) {
                                             ;
                return ((uint16_t)-1);
            }
            hsd->current_byte = hsd->buffers[hsd->input_index++];
                                                               ;
            if (hsd->input_index == hsd->input_size) {
                hsd->input_index = 0;
                hsd->input_size = 0;
            }
            hsd->bit_index = 0x80;
        }
        accumulator <<= 1;
        if (hsd->current_byte & hsd->bit_index) {
            accumulator |= 0x01;
            if (0) {
                                            ;
            }
        } else {
            if (0) {
                                            ;
            }
        }
        hsd->bit_index >>= 1;
    }
    if (count > 1) { ; }
    return accumulator;
}
HSD_finish_res heatshrink_decoder_finish(heatshrink_decoder *hsd) {
    if (hsd == NULL) { return HSDR_FINISH_ERROR_NULL; }
    switch (hsd->state) {
    case HSDS_TAG_BIT:
        return hsd->input_size == 0 ? HSDR_FINISH_DONE : HSDR_FINISH_MORE;
    case HSDS_BACKREF_INDEX_LSB:
    case HSDS_BACKREF_INDEX_MSB:
    case HSDS_BACKREF_COUNT_LSB:
    case HSDS_BACKREF_COUNT_MSB:
        return hsd->input_size == 0 ? HSDR_FINISH_DONE : HSDR_FINISH_MORE;
    case HSDS_YIELD_LITERAL:
        return hsd->input_size == 0 ? HSDR_FINISH_DONE : HSDR_FINISH_MORE;
    default:
        return HSDR_FINISH_MORE;
    }
}
static void push_byte(heatshrink_decoder *hsd, decoder_output_info *oi, uint8_t byte) {
                                                                              ;
    oi->buf[(*oi->output_size)++] = byte;
    (void)hsd;
}
typedef struct {
  unsigned char *ptr;
  size_t len;
} HeatShrinkPtrInputCallbackInfo;
void heatshrink_ptr_output_cb(unsigned char ch, uint32_t *cbdata);
int heatshrink_ptr_input_cb(uint32_t *cbdata);
void heatshrink_var_output_cb(unsigned char ch, uint32_t *cbdata);
int heatshrink_var_input_cb(uint32_t *cbdata);
uint32_t heatshrink_encode_cb(int (*in_callback)(uint32_t *cbdata), uint32_t *in_cbdata, void (*out_callback)(unsigned char ch, uint32_t *cbdata), uint32_t *out_cbdata);
uint32_t heatshrink_decode_cb(int (*in_callback)(uint32_t *cbdata), uint32_t *in_cbdata, void (*out_callback)(unsigned char ch, uint32_t *cbdata), uint32_t *out_cbdata);
uint32_t heatshrink_encode(unsigned char *in_data, size_t in_len, void (*out_callback)(unsigned char ch, uint32_t *cbdata), uint32_t *out_cbdata);
uint32_t heatshrink_decode(int (*in_callback)(uint32_t *cbdata), uint32_t *in_cbdata, unsigned char *out_data);
void heatshrink_ptr_output_cb(unsigned char ch, uint32_t *cbdata) {
  unsigned char **outPtr = (unsigned char**)cbdata;
  *((*outPtr)++) = ch;
}
int heatshrink_ptr_input_cb(uint32_t *cbdata) {
  HeatShrinkPtrInputCallbackInfo *info = (HeatShrinkPtrInputCallbackInfo *)cbdata;
  if (!info->len) return -1;
  info->len--;
  return *(info->ptr++);
}
void heatshrink_var_output_cb(unsigned char ch, uint32_t *cbdata) {
  JsvStringIterator *it = (JsvStringIterator *)cbdata;
  jsvStringIteratorSetCharAndNext(it, (char)ch);
}
int heatshrink_var_input_cb(uint32_t *cbdata) {
  JsvIterator *it = (JsvIterator *)cbdata;
  int d = -1;
  if (jsvIteratorHasElement(it))
    d = jsvIteratorGetIntegerValue(it) & 0xFF;
  jsvIteratorNext(it);
  return d;
}
uint32_t heatshrink_encode_cb(int (*in_callback)(uint32_t *cbdata), uint32_t *in_cbdata, void (*out_callback)(unsigned char ch, uint32_t *cbdata), uint32_t *out_cbdata) {
  heatshrink_encoder hse;
  uint8_t inBuf[128];
  uint8_t outBuf[128];
  heatshrink_encoder_reset(&hse);
  size_t i;
  size_t count = 0;
  size_t polled = 0;
  int lastByte = 0;
  size_t inBufCount = 0;
  size_t inBufOffset = 0;
  while (lastByte >= 0 || inBufCount>0) {
    if (inBufCount==0) {
      inBufOffset = 0;
      while (inBufCount<128 && lastByte>=0) {
        lastByte = in_callback(in_cbdata);
        if (lastByte >= 0)
          inBuf[inBufCount++] = (uint8_t)lastByte;
      }
    }
    bool ok = heatshrink_encoder_sink(&hse, &inBuf[inBufOffset], inBufCount, &count) >= 0;
    do { if (!(ok)) jsAssertFail("bin/espruino_embedded.c",14481,""); } while(0);( (void)(ok) );
    inBufCount -= count;
    inBufOffset += count;
    if ((inBufCount==0) && (lastByte < 0)) {
      heatshrink_encoder_finish(&hse);
    }
    HSE_poll_res pres;
    do {
      pres = heatshrink_encoder_poll(&hse, outBuf, sizeof(outBuf), &count);
      do { if (!(pres >= 0)) jsAssertFail("bin/espruino_embedded.c",14491,""); } while(0);
      if (out_callback)
        for (i=0;i<count;i++)
          out_callback(outBuf[i], out_cbdata);
      polled += count;
    } while (pres == HSER_POLL_MORE);
    do { if (!(pres == HSER_POLL_EMPTY)) jsAssertFail("bin/espruino_embedded.c",14497,""); } while(0);
    if ((inBufCount==0) && (lastByte < 0)) {
      heatshrink_encoder_finish(&hse);
    }
  }
  return (uint32_t)polled;
}
uint32_t heatshrink_decode_cb(int (*in_callback)(uint32_t *cbdata), uint32_t *in_cbdata, void (*out_callback)(unsigned char ch, uint32_t *cbdata), uint32_t *out_cbdata) {
  heatshrink_decoder hsd;
  uint8_t inBuf[128];
  uint8_t outBuf[128];
  heatshrink_decoder_reset(&hsd);
  size_t i;
  size_t count = 0;
  size_t polled = 0;
  int lastByte = 0;
  size_t inBufCount = 0;
  size_t inBufOffset = 0;
  while (lastByte >= 0 || inBufCount>0) {
    if (inBufCount==0) {
      inBufOffset = 0;
      while (inBufCount<128 && lastByte>=0) {
        lastByte = in_callback(in_cbdata);
        if (lastByte >= 0)
          inBuf[inBufCount++] = (uint8_t)lastByte;
      }
    }
    bool ok = heatshrink_decoder_sink(&hsd, &inBuf[inBufOffset], inBufCount, &count) >= 0;
    do { if (!(ok)) jsAssertFail("bin/espruino_embedded.c",14530,""); } while(0);( (void)(ok) );
    inBufCount -= count;
    inBufOffset += count;
    if ((inBufCount==0) && (lastByte < 0)) {
      heatshrink_decoder_finish(&hsd);
    }
    HSD_poll_res pres;
    do {
      pres = heatshrink_decoder_poll(&hsd, outBuf, sizeof(outBuf), &count);
      do { if (!(pres >= 0)) jsAssertFail("bin/espruino_embedded.c",14540,""); } while(0);
      if (out_callback)
        for (i=0;i<count;i++)
          out_callback(outBuf[i], out_cbdata);
      polled += count;
    } while (pres == HSER_POLL_MORE);
    do { if (!(pres == HSER_POLL_EMPTY)) jsAssertFail("bin/espruino_embedded.c",14546,""); } while(0);
    if (lastByte < 0) {
      heatshrink_decoder_finish(&hsd);
    }
  }
  return (uint32_t)polled;
}
uint32_t heatshrink_encode(unsigned char *in_data, size_t in_len, void (*out_callback)(unsigned char ch, uint32_t *cbdata), uint32_t *out_cbdata) {
  HeatShrinkPtrInputCallbackInfo cbi;
  cbi.ptr = in_data;
  cbi.len = in_len;
  return heatshrink_encode_cb(heatshrink_ptr_input_cb, (uint32_t*)&cbi, out_callback, out_cbdata);
}
uint32_t heatshrink_decode(int (*in_callback)(uint32_t *cbdata), uint32_t *in_cbdata, unsigned char *out_data) {
  unsigned char *dataptr = out_data;
  return heatshrink_decode_cb(in_callback, in_cbdata, out_data?heatshrink_ptr_output_cb:NULL, out_data?(uint32_t*)&dataptr:NULL);
}
struct ejs *activeEJS = NULL;
void jshInterruptOn() {}
void jshInterruptOff() {}
bool jshIsInInterrupt() { return false; }
void jsiConsolePrintf(const char *fmt, ...) {
  va_list argp;
  va_start(argp, fmt);
  vcbprintf(vcbprintf_callback_jsiConsolePrintString,0, fmt, argp);
  va_end(argp);
}
void jsiConsolePrintStringVar(JsVar *v) {
  jsiConsolePrintf("%v", v);
}
bool jsiFreeMoreMemory() { return false; }
void jshKickWatchDog() { }
void jsiConsoleRemoveInputLine() {}
JsSysTime jshGetTimeFromMilliseconds(JsVarFloat ms) {
  return (JsSysTime)(ms*1000);
}
JsVarFloat jshGetMillisecondsFromTime(JsSysTime time) {
  return ((JsVarFloat)time)/1000;
}
JsVar *jsvFindOrCreateRoot() {
  return jsvLockAgain(activeEJS->root);
}
JsSysTime jshGetSystemTime() {
  return ejs_get_microseconds();
}
void jsiConsolePrintString(const char *str) {
  ejs_print(str);
}
void vcbprintf_callback_jsiConsolePrintString(const char *str, void* user_data) {
  ( (void)(user_data) );
  jsiConsolePrintString(str);
}
void ejs_set_instance(struct ejs *ejs) {
  if (activeEJS) ejs_unset_instance();
  execInfo.hiddenRoot = ejs->hiddenRoot;
  execInfo.root = ejs->root;
  execInfo.baseScope = ejs->root;
  jsFlags = (JsFlags)ejs->jsFlags;
  jsErrorFlags = (JsErrorFlags)ejs->jsErrorFlags;
  activeEJS = ejs;
}
void ejs_unset_instance() {
  if (!activeEJS) return;
  activeEJS->jsFlags = (unsigned char)jsFlags;
  activeEJS->jsErrorFlags = (unsigned char)jsErrorFlags;
  execInfo.hiddenRoot = NULL;
  execInfo.root = NULL;
  execInfo.baseScope = NULL;
  jsFlags = 0;
  jsErrorFlags = 0;
  activeEJS = NULL;
}
struct ejs *ejs_get_active_instance() {
  return activeEJS;
}
void ejs_clear_exception() {
  if (activeEJS && activeEJS->exception) {
    jsvUnLock(activeEJS->exception);
    activeEJS->exception = NULL;
  }
}
bool ejs_create(unsigned int varCount) {
  jsVars = NULL;
  jswHWInit();
  jsvInit(varCount);
  return jsVars!=NULL;
}
struct ejs *ejs_create_instance(unsigned int varCount) {
  struct ejs *ejs = (struct ejs*)malloc(sizeof(struct ejs));
  if (!ejs) return 0;
  ejs->exception = NULL;
  ejs->root = jsvRef(jsvNewWithFlags(JSV_ROOT));
  activeEJS = ejs;
  jspInit();
  ejs->hiddenRoot = execInfo.hiddenRoot;
  return ejs;
}
void ejs_destroy_instance(struct ejs *ejs) {
  ejs_set_instance(ejs);
  ejs_clear_exception();
  jspKill();
  jsvUnLock(ejs->root);
  free(ejs);
}
void ejs_destroy() {
  jsvKill();
}
JsVar *ejs_catch_exception() {
  JsVar *exception = jspGetException();
  if (exception) {
    ejs_clear_exception();
    activeEJS->exception = exception;
    jsiConsolePrintf("Uncaught %v\n", exception);
    if (jsvIsObject(exception)) {
      JsVar *stackTrace = jsvObjectGetChildIfExists(exception, "stack");
      if (stackTrace) {
        jsiConsolePrintf("%v\n", stackTrace);
        jsvUnLock(stackTrace);
      }
    }
  }
  return exception;
}
JsVar *ejs_exec(struct ejs *ejs, const char *src, bool stringIsStatic) {
  ejs_set_instance(ejs);
  ejs_clear_exception();
  JsVar *v = jspEvaluate(src, stringIsStatic);
  ejs_catch_exception();
  ejs_unset_instance();
  return v;
}
JsVar *ejs_execf(struct ejs *ejs, JsVar *func, JsVar *thisArg, int argCount, JsVar **argPtr) {
  ejs_set_instance(ejs);
  ejs_clear_exception();
  JsVar *v = jspExecuteFunction(func, thisArg, argCount, argPtr);
  ejs_catch_exception();
  ejs_unset_instance();
  return v;
}
const JshPinInfo pinInfo[33] = {
           { JSH_PORTD, JSH_PIN0+0, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+1, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+2, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+3, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+4, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+5, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+6, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+7, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+8, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+9, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+10, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+11, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+12, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+13, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+14, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+15, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+16, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+17, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+18, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+19, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+20, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+21, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+22, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+23, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+24, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+25, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+26, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+27, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+28, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+29, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+30, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+31, JSH_ANALOG_NONE, { } },
           { JSH_PORTD, JSH_PIN0+32, JSH_ANALOG_NONE, { } },
};
JsVar *jswrap_array_constructor(JsVar *args) {
  do { if (!(args)) jsAssertFail("bin/espruino_embedded.c",14839,""); } while(0);
  if (jsvGetArrayLength(args)==1) {
    JsVar *firstArg = jsvSkipNameAndUnLock(jsvGetArrayItem(args,0));
    if (jsvIsNumeric(firstArg)) {
      JsVarFloat f = jsvGetFloat(firstArg);
      JsVarInt count = jsvGetInteger(firstArg);
      jsvUnLock(firstArg);
      if (f!=count || count<0) {
        jsExceptionHere(JSET_ERROR, "Invalid array length");
        return 0;
      } else {
        JsVar *arr = jsvNewEmptyArray();
        if (!arr) return 0;
        jsvSetArrayLength(arr, count, false);
        return arr;
      }
    } else {
      jsvUnLock(firstArg);
    }
  }
  return jsvLockAgain(args);
}
JsVar *jswrap_array_indexOf(JsVar *parent, JsVar *value, JsVarInt startIdx) {
  JsVar *idxName = jsvGetIndexOfFull(parent, value, false , true , startIdx);
  if (idxName == 0) return jsvNewFromInteger(-1);
  return jsvNewFromInteger(jsvGetIntegerAndUnLock(idxName));
}
bool jswrap_array_includes(JsVar *arr, JsVar *value, JsVarInt startIdx) {
  if (startIdx<0) startIdx+=jsvGetLength(arr);
  if (startIdx<0) startIdx=0;
  bool isNaN = jsvIsFloat(value) && isnan(jsvGetFloat(value));
  if (!jsvIsIterable(arr)) return 0;
  JsvIterator it;
  jsvIteratorNew(&it, arr, jsvIsUndefined(value) ? JSIF_EVERY_ARRAY_ELEMENT : JSIF_DEFINED_ARRAY_ElEMENTS);
  while (jsvIteratorHasElement(&it)) {
    JsVar *childIndex = jsvIteratorGetKey(&it);
    if (jsvIsInt(childIndex) && jsvGetInteger(childIndex)>=startIdx) {
      JsVar *childValue = jsvIteratorGetValue(&it);
      if (childValue==value ||
          jsvMathsOpTypeEqual(childValue, value) ||
          (isNaN && jsvIsFloat(childValue) && isnan(jsvGetFloat(childValue)))) {
        jsvUnLock2(childIndex,childValue);
        jsvIteratorFree(&it);
        return true;
      }
      jsvUnLock(childValue);
    }
    jsvUnLock(childIndex);
    jsvIteratorNext(&it);
  }
  jsvIteratorFree(&it);
  return false;
}
JsVar *jswrap_array_join(JsVar *parent, JsVar *filler) {
  if (!jsvIsIterable(parent)) return 0;
  if (jsvIsUndefined(filler))
    filler = jsvNewFromString(",");
  else
    filler = jsvAsString(filler);
  if (!filler) return 0;
  JsVar *str = jsvArrayJoin(parent, filler, true );
  jsvUnLock(filler);
  return str;
}
JsVarInt jswrap_array_push(JsVar *parent, JsVar *args) {
  if (!jsvIsArray(parent)) return -1;
  JsVarInt len = -1;
  JsvObjectIterator it;
  jsvObjectIteratorNew(&it, args);
  while (jsvObjectIteratorHasValue(&it)) {
    JsVar *el = jsvObjectIteratorGetValue(&it);
    len = jsvArrayPush(parent, el);
    jsvUnLock(el);
    jsvObjectIteratorNext(&it);
  }
  jsvObjectIteratorFree(&it);
  if (len<0)
    len = jsvGetArrayLength(parent);
  return len;
}
typedef enum {
  RETURN_BOOL,
  RETURN_ARRAY,
  RETURN_ARRAY_ELEMENT,
  RETURN_ARRAY_INDEX
} AIWCReturnType;
static JsVar *_jswrap_array_iterate_with_callback(
    JsVar *parent,
    JsVar *funcVar,
    JsVar *thisVar,
    AIWCReturnType returnType,
    bool isBoolCallback,
    bool expectedValue
    ) {
  if (!jsvIsIterable(parent)) {
    jsExceptionHere(JSET_ERROR, "Must be called on something iterable");
    return 0;
  }
  if (!jsvIsFunction(funcVar)) {
    jsExceptionHere(JSET_ERROR, "First argument must be a function");
    return 0;
  }
  if (!jsvIsUndefined(thisVar) && !jsvIsObject(thisVar)) {
    jsExceptionHere(JSET_ERROR, "Second argument must be Object or undefined");
    return 0;
  }
  JsVar *result = 0;
  if (returnType == RETURN_ARRAY)
    result = jsvNewEmptyArray();
  bool isDone = false;
  if (result || returnType!=RETURN_ARRAY) {
    JsvIterator it;
    jsvIteratorNew(&it, parent, JSIF_DEFINED_ARRAY_ElEMENTS);
    while (jsvIteratorHasElement(&it) && !isDone) {
      JsVar *index = jsvIteratorGetKey(&it);
      if (jsvIsInt(index)) {
        JsVarInt idxValue = jsvGetInteger(index);
        JsVar *value, *args[3], *cb_result;
        value = jsvIteratorGetValue(&it);
        args[0] = value;
        args[1] = jsvNewFromInteger(idxValue);
        args[2] = parent;
        jsvIteratorNext(&it);
        cb_result = jspeFunctionCall(funcVar, 0, thisVar, false, 3, args);
        jsvUnLock(args[1]);
        if (cb_result) {
          bool matched;
          if (isBoolCallback)
            matched = (jsvGetBool(cb_result) == expectedValue);
          if (returnType == RETURN_ARRAY) {
            if (isBoolCallback) {
              if (matched) {
                jsvArrayPush(result, value);
              }
            } else {
              JsVar *name = jsvNewFromInteger(idxValue);
              if (name) {
                name = jsvMakeIntoVariableName(name, cb_result);
                jsvAddName(result, name);
                jsvUnLock(name);
              }
            }
          } else if (isBoolCallback) {
            if (returnType == RETURN_ARRAY_ELEMENT ||
                returnType == RETURN_ARRAY_INDEX) {
              if (matched) {
                result = (returnType == RETURN_ARRAY_ELEMENT) ?
                    jsvLockAgain(value) :
                    jsvNewFromInteger(jsvGetInteger(index));
                isDone = true;
              }
            } else if (!matched)
              isDone = true;
          }
          jsvUnLock(cb_result);
        }
        jsvUnLock(value);
      } else {
        jsvIteratorNext(&it);
      }
      jsvUnLock(index);
    }
    jsvIteratorFree(&it);
  }
  if (returnType == RETURN_BOOL && isBoolCallback) {
    result = jsvNewFromBool(isDone != expectedValue);
  }
  return result;
}
JsVar *jswrap_array_map(JsVar *parent, JsVar *funcVar, JsVar *thisVar) {
  return _jswrap_array_iterate_with_callback(parent, funcVar, thisVar, RETURN_ARRAY, false, false);
}
void jswrap_array_forEach(JsVar *parent, JsVar *funcVar, JsVar *thisVar) {
  _jswrap_array_iterate_with_callback(parent, funcVar, thisVar, RETURN_BOOL, false, false);
}
JsVar *jswrap_array_filter(JsVar *parent, JsVar *funcVar, JsVar *thisVar) {
  return _jswrap_array_iterate_with_callback(parent, funcVar, thisVar, RETURN_ARRAY, true, true);
}
JsVar *jswrap_array_find(JsVar *parent, JsVar *funcVar) {
  return _jswrap_array_iterate_with_callback(parent, funcVar, 0, RETURN_ARRAY_ELEMENT, true, true);
}
JsVar *jswrap_array_findIndex(JsVar *parent, JsVar *funcVar) {
  JsVar *v = _jswrap_array_iterate_with_callback(parent, funcVar, 0, RETURN_ARRAY_INDEX, true, true);
  if (v) return v;
  return jsvNewFromInteger(-1);
}
JsVar *jswrap_array_some(JsVar *parent, JsVar *funcVar, JsVar *thisVar) {
  return _jswrap_array_iterate_with_callback(parent, funcVar, thisVar, RETURN_BOOL, true, false);
}
JsVar *jswrap_array_every(JsVar *parent, JsVar *funcVar, JsVar *thisVar) {
  return _jswrap_array_iterate_with_callback(parent, funcVar, thisVar, RETURN_BOOL, true, true);
}
JsVar *jswrap_array_reduce(JsVar *parent, JsVar *funcVar, JsVar *initialValue) {
  if (!jsvIsIterable(parent)) {
    jsExceptionHere(JSET_ERROR, "Must be called on something iterable");
    return 0;
  }
  if (!jsvIsFunction(funcVar)) {
    jsExceptionHere(JSET_ERROR, "First argument must be a function");
    return 0;
  }
  JsVar *previousValue = jsvLockAgainSafe(initialValue);
  JsvIterator it;
  jsvIteratorNew(&it, parent, JSIF_DEFINED_ARRAY_ElEMENTS);
  if (!previousValue) {
    bool isDone = false;
    while (!isDone && jsvIteratorHasElement(&it)) {
      JsVar *index = jsvIteratorGetKey(&it);
      if (jsvIsInt(index)) {
        previousValue = jsvIteratorGetValue(&it);
        isDone = true;
      }
      jsvUnLock(index);
      jsvIteratorNext(&it);
    }
    if (!previousValue) {
      jsExceptionHere(JSET_ERROR, "Used on empty array with no initial value");
    }
  }
  while (jsvIteratorHasElement(&it)) {
    JsVar *index = jsvIteratorGetKey(&it);
    if (jsvIsInt(index)) {
      JsVarInt idxValue = jsvGetInteger(index);
      JsVar *args[4];
      args[0] = previousValue;
      args[1] = jsvIteratorGetValue(&it);
      args[2] = jsvNewFromInteger(idxValue);
      args[3] = parent;
      previousValue = jspeFunctionCall(funcVar, 0, 0, false, 4, args);
      jsvUnLockMany(3,args);
    }
    jsvUnLock(index);
    jsvIteratorNext(&it);
  }
  jsvIteratorFree(&it);
  return previousValue;
}
JsVar *jswrap_array_splice(JsVar *parent, JsVarInt index, JsVar *howManyVar, JsVar *elements) {
  if (!jsvIsArray(parent)) return 0;
  JsVarInt len = jsvGetArrayLength(parent);
  if (index<0) index+=len;
  if (index<0) index=0;
  if (index>len) index=len;
  JsVarInt howMany = len;
  if (jsvIsNumeric(howManyVar)) howMany = jsvGetInteger(howManyVar);
  if (howMany > len-index) howMany = len-index;
  JsVarInt newItems = jsvGetArrayLength(elements);
  JsVarInt shift = newItems-howMany;
  bool needToAdd = false;
  JsVar *result = jsvNewEmptyArray();
  JsvObjectIterator it;
  jsvObjectIteratorNew(&it, parent);
  while (jsvObjectIteratorHasValue(&it) && !needToAdd) {
    bool goToNext = true;
    JsVar *idxVar = jsvObjectIteratorGetKey(&it);
    if (idxVar && jsvIsInt(idxVar)) {
      JsVarInt idx = jsvGetInteger(idxVar);
      if (idx<index) {
      } else if (idx<index+howMany) {
        if (result) {
          JsVar *el = jsvObjectIteratorGetValue(&it);
          jsvArrayPushAndUnLock(result, el);
        }
        goToNext = false;
        JsVar *toRemove = jsvObjectIteratorGetKey(&it);
        jsvObjectIteratorNext(&it);
        jsvRemoveChildAndUnLock(parent, toRemove);
      } else {
        needToAdd = true;
        goToNext = false;
      }
    }
    jsvUnLock(idxVar);
    if (goToNext) jsvObjectIteratorNext(&it);
  }
  JsVar *beforeIndex = jsvObjectIteratorGetKey(&it);
  JsvObjectIterator itElement;
  jsvObjectIteratorNew(&itElement, elements);
  while (jsvObjectIteratorHasValue(&itElement)) {
    JsVar *element = jsvObjectIteratorGetValue(&itElement);
    jsvArrayInsertBefore(parent, beforeIndex, element);
    jsvUnLock(element);
    jsvObjectIteratorNext(&itElement);
  }
  jsvObjectIteratorFree(&itElement);
  jsvUnLock(beforeIndex);
  while (jsvObjectIteratorHasValue(&it)) {
    JsVar *idxVar = jsvObjectIteratorGetKey(&it);
    if (idxVar && jsvIsInt(idxVar)) {
      jsvSetInteger(idxVar, jsvGetInteger(idxVar)+shift);
    }
    jsvUnLock(idxVar);
    jsvObjectIteratorNext(&it);
  }
  jsvObjectIteratorFree(&it);
  jsvSetArrayLength(parent, len + shift, false);
  return result;
}
JsVar *jswrap_array_splice_i(JsVar *parent, JsVarInt index, JsVarInt howMany, JsVar *elements) {
  JsVar *howManyVar = jsvNewFromInteger(howMany);
  JsVar *arr = jswrap_array_splice(parent, index, howManyVar, elements);
  jsvUnLock(howManyVar);
  return arr;
}
JsVar *jswrap_array_shift(JsVar *parent) {
  JsVar *arr = jswrap_array_splice_i(parent, 0, 1, NULL);
  JsVar *el = 0;
  if (jsvIsArray(arr))
    el = jsvSkipNameAndUnLock(jsvArrayPop(arr));
  jsvUnLock(arr);
  return el;
}
JsVarInt jswrap_array_unshift(JsVar *parent, JsVar *elements) {
  jsvUnLock(jswrap_array_splice_i(parent, 0, 0, elements));
  return jsvGetLength(parent);
}
JsVar *jswrap_array_slice(JsVar *parent, JsVarInt start, JsVar *endVar) {
  JsVarInt len = jsvGetLength(parent);
  JsVarInt end = len;
  if (!jsvIsUndefined(endVar))
    end = jsvGetInteger(endVar);
  JsVarInt k = 0;
  JsVarInt final = len;
  JsVar *array = jsvNewEmptyArray();
  if (!array) return 0;
  if (start<0) k = ((((len + start))>(0))?((len + start)):(0));
  else k = (((start)<(len))?(start):(len));
  if (end<0) final = ((((len + end))>(0))?((len + end)):(0));
  else final = (((end)<(len))?(end):(len));
  bool isDone = false;
  JsvIterator it;
  jsvIteratorNew(&it, parent, JSIF_EVERY_ARRAY_ELEMENT);
  while (jsvIteratorHasElement(&it) && !isDone) {
    JsVarInt idx = jsvGetIntegerAndUnLock(jsvIteratorGetKey(&it));
    if (idx < k) {
      jsvIteratorNext(&it);
    } else {
      if (k < final) {
        jsvArrayPushAndUnLock(array, jsvIteratorGetValue(&it));
        jsvIteratorNext(&it);
        k++;
      } else {
        isDone = true;
      }
    }
  }
  jsvIteratorFree(&it);
  return array;
}
__attribute__ ((noinline)) static JsVarInt _jswrap_array_sort_compare(JsVar *a, JsVar *b, JsVar *compareFn) {
  if(jsvIsUndefined(a)) {
    return 1;
  } else if(jsvIsUndefined(b)) {
    return -1;
  } else if (compareFn) {
    JsVar *args[2] = {a,b};
    JsVarFloat f = jsvGetFloatAndUnLock(jspeFunctionCall(compareFn, 0, 0, false, 2, args));
    if (f==0) return 0;
    return (f<0)?-1:1;
  } else {
    JsVar *sa = jsvAsString(a);
    JsVar *sb = jsvAsString(b);
    JsVarInt r = jsvCompareString(sa,sb, 0, 0, false);
    jsvUnLock2(sa, sb);
    return r;
  }
}
__attribute__ ((noinline)) static void _jswrap_array_sort(JsvIterator *head, int n, JsVar *compareFn) {
  if (n < 2) return;
  JsvIterator pivot;
  jsvIteratorClone(&pivot, head);
  bool pivotLowest = true;
  JsVar *pivotValue = jsvIteratorGetValue(&pivot);
  int nlo = 0, nhigh = 0;
  JsvIterator it;
  jsvIteratorClone(&it, head);
  jsvIteratorNext(&it);
  while (--n && !jspIsInterrupted()) {
    JsVar *itValue = jsvIteratorGetValue(&it);
    JsVarInt cmp = _jswrap_array_sort_compare(itValue, pivotValue, compareFn);
    if (cmp<=0) {
      if (cmp<0) pivotLowest = false;
      nlo++;
      jsvIteratorSetValue(&pivot, itValue);
      jsvIteratorNext(&pivot);
      jsvUnLock(jsvIteratorSetValue(&it, jsvIteratorGetValue(&pivot)));
      jsvIteratorSetValue(&pivot, pivotValue);
    } else {
      nhigh++;
    }
    jsvUnLock(itValue);
    jsvIteratorNext(&it);
  }
  jsvIteratorFree(&it);
  jsvUnLock(pivotValue);
  if (jspIsInterrupted()) {
    jsvIteratorFree(&pivot);
    return;
  }
  jsvIteratorNext(&pivot);
  _jswrap_array_sort(&pivot, nhigh, compareFn);
  jsvIteratorFree(&pivot);
  if (!pivotLowest)
    _jswrap_array_sort(head, nlo, compareFn);
}
JsVar *jswrap_array_sort (JsVar *array, JsVar *compareFn) {
  if (!jsvIsUndefined(compareFn) && !jsvIsFunction(compareFn)) {
    jsExceptionHere(JSET_ERROR, "Expecting compare function, got %t", compareFn);
    return 0;
  }
  JsvIterator it;
  int n=0;
  if (jsvIsArray(array) || jsvIsObject(array)) {
    jsvIteratorNew(&it, array, JSIF_EVERY_ARRAY_ELEMENT);
    while (jsvIteratorHasElement(&it)) {
      n++;
      jsvIteratorNext(&it);
    }
    jsvIteratorFree(&it);
  } else {
    n = (int)jsvGetLength(array);
  }
  jsvIteratorNew(&it, array, JSIF_EVERY_ARRAY_ELEMENT);
  _jswrap_array_sort(&it, n, compareFn);
  jsvIteratorFree(&it);
  return jsvLockAgain(array);
}
JsVar *jswrap_array_concat(JsVar *parent, JsVar *args) {
  JsVar *result = jsvNewEmptyArray();
  JsvObjectIterator argsIt;
  jsvObjectIteratorNew(&argsIt, args);
  JsVar *source = jsvLockAgain(parent);
  do {
    if (jsvIsArray(source)) {
      jsvArrayPushAll(result, source, false);
    } else
      jsvArrayPush(result, source);
    jsvUnLock(source);
    source = jsvObjectIteratorHasValue(&argsIt) ? jsvObjectIteratorGetValue(&argsIt) : 0;
    jsvObjectIteratorNext(&argsIt);
  } while (source);
  jsvObjectIteratorFree(&argsIt);
  return result;
}
JsVar *jswrap_array_fill(JsVar *parent, JsVar *value, JsVarInt start, JsVar *endVar) {
  if (!jsvIsIterable(parent)) return 0;
  JsVarInt length = jsvGetLength(parent);
  if (start < 0) start = start + length;
  if (start < 0) return 0;
  JsVarInt end = jsvIsNumeric(endVar) ? jsvGetInteger(endVar) : length;
  if (end < 0) end = end + length;
  if (end < 0) return 0;
  JsvIterator it;
  jsvIteratorNew(&it, parent, JSIF_EVERY_ARRAY_ELEMENT);
  while (jsvIteratorHasElement(&it) && !jspIsInterrupted()) {
    JsVarInt idx = jsvGetIntegerAndUnLock(jsvIteratorGetKey(&it));
    if (idx>=start && idx<end) {
      jsvIteratorSetValue(&it, value);
    }
    jsvIteratorNext(&it);
  }
  jsvIteratorFree(&it);
  return jsvLockAgain(parent);
}
void _jswrap_array_reverse_block(JsVar *parent, JsvIterator *it, int items) {
  do { if (!(items > 1)) jsAssertFail("bin/espruino_embedded.c",15806,""); } while(0);
  JsvIterator ita, itb;
  jsvIteratorClone(&ita, it);
  jsvIteratorClone(&itb, it);
  int i;
  for (i=(items+1)/2;i>0;i--)
    jsvIteratorNext(&itb);
  if (items > 3) {
    _jswrap_array_reverse_block(parent, &ita, items/2);
    _jswrap_array_reverse_block(parent, &itb, items/2);
  }
  for (i=items/2;i>0;i--) {
    JsVar *va = jsvIteratorGetValue(&ita);
    JsVar *vb = jsvIteratorGetValue(&itb);
    jsvIteratorSetValue(&ita, vb);
    jsvIteratorSetValue(&itb, va);
    jsvUnLock2(va, vb);
    if (jsvIsArray(parent)) {
      JsVar *ka = jsvIteratorGetKey(&ita);
      JsVar *kb = jsvIteratorGetKey(&itb);
      JsVarInt kva = jsvGetInteger(ka);
      JsVarInt kvb = jsvGetInteger(kb);
      jsvSetInteger(ka, kvb);
      jsvSetInteger(kb, kva);
      jsvUnLock2(ka, kb);
    }
    jsvIteratorNext(&ita);
    jsvIteratorNext(&itb);
  }
  jsvIteratorFree(&ita);
  jsvIteratorFree(&itb);
}
JsVar *jswrap_array_reverse(JsVar *parent) {
  if (!jsvIsIterable(parent) || jsvIsObject(parent)) return 0;
  int len = 0;
  if (jsvIsArray(parent)) {
    JsvIterator it;
    jsvIteratorNew(&it, parent, JSIF_DEFINED_ARRAY_ElEMENTS);
    while (jsvIteratorHasElement(&it)) {
      JsVar *k = jsvIteratorGetKey(&it);
      if (jsvIsInt(k)) len++;
      jsvUnLock(k);
      jsvIteratorNext(&it);
    }
    jsvIteratorFree(&it);
  } else
    len = jsvGetLength(parent);
  JsvIterator it;
  jsvIteratorNew(&it, parent, JSIF_DEFINED_ARRAY_ElEMENTS);
  if (len>1) {
    _jswrap_array_reverse_block(parent, &it, len);
  }
  if (jsvIsArray(parent)) {
    JsVarInt last = jsvGetArrayLength(parent)-1;
    while (jsvIteratorHasElement(&it)) {
      JsVar *k = jsvIteratorGetKey(&it);
      if (jsvIsInt(k))
        jsvSetInteger(k, last-jsvGetInteger(k));
      jsvUnLock(k);
      jsvIteratorNext(&it);
    }
  }
  jsvIteratorFree(&it);
  return jsvLockAgain(parent);
}
JsVar *jswrap_arraybuffer_constructor(JsVarInt byteLength) {
  if (byteLength < 0) {
    jsExceptionHere(JSET_ERROR, "Invalid length for ArrayBuffer");
    return 0;
  }
  if (byteLength > 0xFFFFFF) {
    jsExceptionHere(JSET_ERROR, "ArrayBuffer too long");
    return 0;
  }
  JsVar *arrData = 0;
  if (byteLength > ((4 + ((14*3 + 0)>>3)) + (4 + ((14*3 + 0 + 8)>>3))))
    arrData = jsvNewFlatStringOfLength((unsigned int)byteLength);
  if (!arrData)
    arrData = jsvNewStringOfLength((unsigned int)byteLength, NULL);
  if (!arrData) return 0;
  JsVar *v = jsvNewArrayBufferFromString(arrData, (unsigned int)byteLength);
  jsvUnLock(arrData);
  return v;
}
JsVar *jswrap_typedarray_constructor(JsVarDataArrayBufferViewType type, JsVar *arr, JsVarInt byteOffset, JsVarInt length) {
  JsVar *arrayBuffer = 0;
  bool copyData = false;
  if (byteOffset < 0 || byteOffset > 65535) {
    jsExceptionHere(JSET_ERROR, "byteOffset too large (or negative)");
    return 0;
  }
  if (jsvIsArrayBuffer(arr) && arr->varData.arraybuffer.type==ARRAYBUFFERVIEW_ARRAYBUFFER) {
    arrayBuffer = jsvLockAgain(arr);
  } else if (jsvIsNumeric(arr)) {
    length = jsvGetInteger(arr);
    byteOffset = 0;
    arrayBuffer = jswrap_arraybuffer_constructor((int)(size_t)((type)&ARRAYBUFFERVIEW_MASK_SIZE)*length);
  } else if (jsvIsArray(arr) || jsvIsArrayBuffer(arr)) {
    length = (JsVarInt)jsvGetLength(arr);
    byteOffset = 0;
    arrayBuffer = jswrap_arraybuffer_constructor((int)(size_t)((type)&ARRAYBUFFERVIEW_MASK_SIZE)*length);
    copyData = true;
  }
  if (!arrayBuffer) {
    jsExceptionHere(JSET_ERROR, "Unsupported first argument of type %t", arr);
    return 0;
  }
  if (length==0) {
    length = ((JsVarInt)jsvGetArrayBufferLength(arrayBuffer)-byteOffset) / (JsVarInt)(size_t)((type)&ARRAYBUFFERVIEW_MASK_SIZE);
    if (length<0) length=0;
  }
  JsVar *typedArr = jsvNewWithFlags(JSV_ARRAYBUFFER);
  if (typedArr) {
    typedArr->varData.arraybuffer.type = type;
    typedArr->varData.arraybuffer.byteOffset = (unsigned short)byteOffset;
    typedArr->varData.arraybuffer.length = (JsVarArrayBufferLength)length;
    jsvSetFirstChild(typedArr, jsvGetRef(jsvRef(arrayBuffer)));
    if (copyData) {
      JsvIterator it;
      jsvIteratorNew(&it, arr, JSIF_DEFINED_ARRAY_ElEMENTS);
      while (jsvIteratorHasElement(&it)) {
        JsVar *idx = jsvIteratorGetKey(&it);
        if (jsvIsInt(idx)) {
          JsVar *val = jsvIteratorGetValue(&it);
          jsvArrayBufferSet(typedArr, (size_t)jsvGetInteger(idx), val);
          jsvUnLock(val);
        }
        jsvUnLock(idx);
        jsvIteratorNext(&it);
      }
      jsvIteratorFree(&it);
    }
  }
  jsvUnLock(arrayBuffer);
  return typedArr;
}
void jswrap_arraybufferview_set(JsVar *parent, JsVar *arr, int offset) {
  if (!(jsvIsString(arr) || jsvIsArray(arr) || jsvIsArrayBuffer(arr))) {
    jsExceptionHere(JSET_ERROR, "First argument must be Array, not %t", arr);
    return;
  }
  if (jsvIsArrayBuffer(parent) && jsvIsArrayBuffer(arr)) {
    JsVar *sa = jsvGetArrayBufferBackingString(parent, NULL);
    JsVar *sb = jsvGetArrayBufferBackingString(arr, NULL);
    bool setBackwards = sa == sb && arr->varData.arraybuffer.byteOffset <=
        parent->varData.arraybuffer.byteOffset + offset*(int)(size_t)((parent->varData.arraybuffer.type)&ARRAYBUFFERVIEW_MASK_SIZE);
    jsvUnLock2(sa,sb);
    if (setBackwards) {
      int len = (int)jsvGetArrayBufferLength(arr);
      for (int i=len-1;i>=0;i--) {
        JsVar *v = jsvArrayBufferGet(arr, (size_t)i);
        jsvArrayBufferSet(parent, (size_t)(offset+i), v);
        jsvUnLock(v);
      }
      return;
    }
  }
  JsvIterator itsrc;
  jsvIteratorNew(&itsrc, arr, JSIF_EVERY_ARRAY_ELEMENT);
  JsvArrayBufferIterator itdst;
  jsvArrayBufferIteratorNew(&itdst, parent, (size_t)offset);
  bool useInts = !(((itdst.type)&ARRAYBUFFERVIEW_FLOAT)!=0) || jsvIsString(arr);
  while (jsvIteratorHasElement(&itsrc) && jsvArrayBufferIteratorHasElement(&itdst)) {
    if (useInts) {
      jsvArrayBufferIteratorSetIntegerValue(&itdst, jsvIteratorGetIntegerValue(&itsrc));
    } else {
      JsVar *value = jsvIteratorGetValue(&itsrc);
      jsvArrayBufferIteratorSetValue(&itdst, value, false );
      jsvUnLock(value);
    }
    jsvArrayBufferIteratorNext(&itdst);
    jsvIteratorNext(&itsrc);
  }
  jsvArrayBufferIteratorFree(&itdst);
  jsvIteratorFree(&itsrc);
}
JsVar *jswrap_arraybufferview_map(JsVar *parent, JsVar *funcVar, JsVar *thisVar) {
  if (!jsvIsArrayBuffer(parent)) {
    jsExceptionHere(JSET_ERROR, "Can only be called on an ArrayBufferView");
    return 0;
  }
  if (!jsvIsFunction(funcVar)) {
    jsExceptionHere(JSET_ERROR, "First argument must be a function");
    return 0;
  }
  if (!jsvIsUndefined(thisVar) && !jsvIsObject(thisVar)) {
    jsExceptionHere(JSET_ERROR, "Second argument must be Object or undefined");
    return 0;
  }
  JsVarDataArrayBufferViewType arrayBufferType = parent->varData.arraybuffer.type;
  JsVar *array = jsvNewTypedArray(arrayBufferType, (JsVarInt)jsvGetArrayBufferLength(parent));
  if (!array) return 0;
  JsvIterator it;
  jsvIteratorNew(&it, parent, JSIF_EVERY_ARRAY_ELEMENT);
  JsvArrayBufferIterator itdst;
  jsvArrayBufferIteratorNew(&itdst, array, 0);
  while (jsvIteratorHasElement(&it)) {
    JsVar *index = jsvIteratorGetKey(&it);
    if (jsvIsInt(index)) {
      JsVarInt idxValue = jsvGetInteger(index);
      JsVar *args[3], *mapped;
      args[0] = jsvIteratorGetValue(&it);
      args[1] = jsvNewFromInteger(idxValue);
      args[2] = parent;
      mapped = jspeFunctionCall(funcVar, 0, thisVar, false, 3, args);
      jsvUnLockMany(2,args);
      if (mapped) {
        jsvArrayBufferIteratorSetValue(&itdst, mapped, false );
        jsvUnLock(mapped);
      }
    }
    jsvUnLock(index);
    jsvIteratorNext(&it);
    jsvArrayBufferIteratorNext(&itdst);
  }
  jsvIteratorFree(&it);
  jsvArrayBufferIteratorFree(&itdst);
  return array;
}
JsVar *jswrap_arraybufferview_subarray(JsVar *parent, JsVarInt begin, JsVar *endVar) {
  if (!jsvIsArrayBuffer(parent)) {
    jsExceptionHere(JSET_ERROR, "ArrayBufferView.subarray can only be called on an ArrayBufferView");
    return 0;
  }
  JsVarInt end = jsvGetInteger(endVar);
  if (!jsvIsNumeric(endVar)) {
    end = (JsVarInt)jsvGetArrayBufferLength(parent);
  }
  if (begin < 0)
    begin += (JsVarInt)jsvGetArrayBufferLength(parent);
  if (end < 0)
    end += (JsVarInt)jsvGetArrayBufferLength(parent);
  if (end < 0) end = 0;
  if (begin > end) {
    begin = 0;
    end = 0;
  }
  JsVarDataArrayBufferViewType arrayBufferType = parent->varData.arraybuffer.type;
  if (begin == end)
    return jsvNewTypedArray(arrayBufferType, 0);
  JsVar *arrayBuffer = jsvLock(jsvGetFirstChild(parent));
  JsVar *result = jswrap_typedarray_constructor(
      arrayBufferType, arrayBuffer,
      parent->varData.arraybuffer.byteOffset + begin * (JsVarInt)(size_t)((arrayBufferType)&ARRAYBUFFERVIEW_MASK_SIZE),
      end-begin);
  jsvUnLock(arrayBuffer);
  return result;
}
static JsVarFloat _jswrap_arraybufferview_sort_float(JsVarFloat a, JsVarFloat b) {
  return a-b;
}
static JsVarInt _jswrap_arraybufferview_sort_int(JsVarInt a, JsVarInt b) {
  return a-b;
}
JsVar *jswrap_arraybufferview_sort(JsVar *array, JsVar *compareFn) {
  if (!jsvIsArrayBuffer(array)) return 0;
  bool isFloat = (((array->varData.arraybuffer.type)&ARRAYBUFFERVIEW_FLOAT)!=0);
  if (compareFn)
    return jswrap_array_sort(array, compareFn);
  compareFn = isFloat ?
      jsvNewNativeFunction(
          (void (*)(void))_jswrap_arraybufferview_sort_float,
          JSWAT_JSVARFLOAT|(JSWAT_JSVARFLOAT<<(((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 ))|(JSWAT_JSVARFLOAT<<((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2))) :
      jsvNewNativeFunction(
                (void (*)(void))_jswrap_arraybufferview_sort_int,
                JSWAT_INT32|(JSWAT_INT32<<(((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 ))|(JSWAT_INT32<<((((JSWAT_MASK+1)== 1)? 0: ((JSWAT_MASK+1)== 2)? 1: ((JSWAT_MASK+1)== 4)? 2: ((JSWAT_MASK+1)== 8)? 3: ((JSWAT_MASK+1)== 16)? 4: ((JSWAT_MASK+1)== 32)? 5: ((JSWAT_MASK+1)== 64)? 6: ((JSWAT_MASK+1)== 128)? 7: ((JSWAT_MASK+1)== 256)? 8: ((JSWAT_MASK+1)== 512)? 9: ((JSWAT_MASK+1)== 1024)?10: ((JSWAT_MASK+1)== 2048)?11: ((JSWAT_MASK+1)== 4096)?12: ((JSWAT_MASK+1)== 8192)?13: ((JSWAT_MASK+1)==16384)?14: ((JSWAT_MASK+1)==32768)?15:10000 )*2)));
  JsVar *r = jswrap_array_sort(array, compareFn);
  jsvUnLock(compareFn);
  return r;
}
JsVar *jswrap_dataview_constructor(JsVar *buffer, int byteOffset, int byteLength) {
  if (!jsvIsArrayBuffer(buffer) ||
      buffer->varData.arraybuffer.type!=ARRAYBUFFERVIEW_ARRAYBUFFER) {
    jsExceptionHere(JSET_TYPEERROR, "Expecting ArrayBuffer, got %t", buffer);
    return 0;
  }
  JsVar *dataview = jspNewObject(0,"DataView");
  if (dataview) {
    jsvObjectSetChild(dataview, "buffer", buffer);
    jsvObjectSetChildAndUnLock(dataview, "byteOffset", jsvNewFromInteger(byteOffset));
    jsvObjectSetChildAndUnLock(dataview, "byteLength", jsvNewFromInteger(
        byteLength?(unsigned int)byteLength:jsvGetArrayBufferLength(buffer)));
  }
  return dataview;
}
JsVar *jswrap_dataview_get(JsVar *dataview, JsVarDataArrayBufferViewType type, int byteOffset, bool littleEndian) {
  JsVar *buffer = jsvObjectGetChildIfExists(dataview, "buffer");
  if (!jsvIsArrayBuffer(buffer)) {
    jsvUnLock(buffer);
    return 0;
  }
  byteOffset += jsvObjectGetIntegerChild(dataview, "byteOffset");
  JsVarInt length = (size_t)((type)&ARRAYBUFFERVIEW_MASK_SIZE);
  JsVar *arr = jswrap_typedarray_constructor(type, buffer, byteOffset, length);
  jsvUnLock(buffer);
  if (!arr) return 0;
  JsvArrayBufferIterator it;
  jsvArrayBufferIteratorNew(&it, arr, 0);
  JsVar *value = jsvArrayBufferIteratorGetValue(&it, !littleEndian);
  jsvArrayBufferIteratorFree(&it);
  jsvUnLock(arr);
  return value;
}
void jswrap_dataview_set(JsVar *dataview, JsVarDataArrayBufferViewType type, int byteOffset, JsVar *value, bool littleEndian) {
  JsVar *buffer = jsvObjectGetChildIfExists(dataview, "buffer");
  if (!jsvIsArrayBuffer(buffer)) {
    jsvUnLock(buffer);
    return;
  }
  byteOffset += jsvObjectGetIntegerChild(dataview, "byteOffset");
  JsVarInt length = (size_t)((type)&ARRAYBUFFERVIEW_MASK_SIZE);
  JsVar *arr = jswrap_typedarray_constructor(type, buffer, byteOffset, length);
  jsvUnLock(buffer);
  if (!arr) return;
  JsvArrayBufferIterator it;
  jsvArrayBufferIteratorNew(&it, arr, 0);
  jsvArrayBufferIteratorSetValue(&it, value, !littleEndian);
  jsvArrayBufferIteratorFree(&it);
  jsvUnLock(arr);
}
const int MSDAY = 24*60*60*1000;
const int BASE_DOW = 4;
const char *MONTHNAMES = "Jan\0Feb\0Mar\0Apr\0May\0Jun\0Jul\0Aug\0Sep\0Oct\0Nov\0Dec";
const char *DAYNAMES = "Sun\0Mon\0Tue\0Wed\0Thu\0Fri\0Sat";
int integerDivideFloor(int a, int b) {
  return (a < 0 ? a-b+1 : a)/b;
}
int getDayNumberFromDate(int y, int m, int d) {
  int ans;
  if (y < -1250000 || y >= 1250000) {
    jsExceptionHere(JSET_ERROR, "Date out of bounds");
    return 0;
  }
  while (m < 2) {
    y--;
    m+=12;
  }
  ans = integerDivideFloor((y),(100));
  return 365*y + integerDivideFloor((y),(4)) - ans + integerDivideFloor((ans),(4)) + 30*m + ((3*m+6)/5) + d - 719531;
}
void getDateFromDayNumber(int day, int *y, int *m, int *date) {
  int a = day + 135081;
  int b,c,d;
  a = integerDivideFloor((a - integerDivideFloor((a),(146097)) + 146095),(36524));
  a = day + a - integerDivideFloor((a),(4));
  b = integerDivideFloor(((a<<2)+2877911),(1461));
  c = a + 719600 - 365*b - integerDivideFloor((b),(4));
  d = (5*c-1)/153;
  if (date) *date=c-30*d-((3*d)/5);
  if (m) {
    if (d<14)
      *m=d-2;
    else
      *m=d-14;
  }
  if (y) {
    if (d>13)
      *y=b+1;
    else
      *y=b;
  }
}
JsVarFloat getDstChangeTime(int y, int dow_number, int dow, int month, int day_offset, int timeOfDay, bool is_start, int dst_offset, int timezone, bool as_local_time) {
  int ans;
  if (dow_number == 4) {
    if (++month > 11) {
      y++;
      month-=12;
    }
  }
  ans = getDayNumberFromDate(y, month, 1);
  if (dow_number == 4) {
    ans += ((14 - ((ans + 4) % 7) + dow) % 7) - 7;
  } else {
    ans += 7 * dow_number + (14 - ((ans + 4) % 7) + dow) % 7;
  }
  ans = (ans + day_offset) * 1440 + timeOfDay;
  if (!as_local_time) {
    ans -= timezone;
    if (!is_start) ans -= dst_offset;
  }
  return 60.0*ans;
}
int jsdGetEffectiveTimeZone(JsVarFloat ms, bool is_local_time, bool *is_dst) {
  JsVar *dst = jsvObjectGetChildIfExists(execInfo.hiddenRoot, "dst");
  if ((dst) && (jsvIsArrayBuffer(dst)) && (jsvGetLength(dst) == 12) && (dst->varData.arraybuffer.type == ARRAYBUFFERVIEW_INT16)) {
    int y;
    JsVarInt dstSetting[12];
    JsvArrayBufferIterator it;
    jsvArrayBufferIteratorNew(&it, dst, 0);
    y = 0;
    while (y < 12) {
      dstSetting[y++]=jsvArrayBufferIteratorGetIntegerValue(&it);
      jsvArrayBufferIteratorNext(&it);
    }
    jsvArrayBufferIteratorFree(&it);
    jsvUnLock(dst);
    if (dstSetting[0]) {
      JsVarFloat sec = ms/1000;
      JsVarFloat dstStart,dstEnd;
      bool dstActive;
      getDateFromDayNumber((int)(sec/86400),&y,0,0);
      dstStart = getDstChangeTime(y, dstSetting[2], dstSetting[3], dstSetting[4], dstSetting[5], dstSetting[6], 1, dstSetting[0], dstSetting[1], is_local_time);
      dstEnd = getDstChangeTime(y, dstSetting[7], dstSetting[8], dstSetting[9], dstSetting[10], dstSetting[11], 0, dstSetting[0], dstSetting[1], is_local_time);
      if (dstStart < dstEnd) {
        dstActive = (sec >= dstStart) && (sec < dstEnd);
      } else {
        dstActive = (sec < dstEnd) || (sec >= dstStart);
      }
      if (is_dst) *is_dst = dstActive;
      return dstActive ? dstSetting[0]+dstSetting[1] : dstSetting[1];
    }
  } else {
    jsvUnLock(dst);
  }
  if (is_dst) *is_dst = false;
  return jsvObjectGetIntegerChild(execInfo.hiddenRoot, "tz");
}
void setCorrectTimeZone(TimeInDay *td) {
  td->zone = 0;
  td->zone = jsdGetEffectiveTimeZone(fromTimeInDay(td),true,&(td->is_dst));
}
TimeInDay getTimeFromMilliSeconds(JsVarFloat ms_in, bool forceGMT) {
  TimeInDay t;
  t.zone = forceGMT ? 0 : jsdGetEffectiveTimeZone(ms_in, false, &(t.is_dst));
  ms_in += t.zone*60000;
  t.daysSinceEpoch = (int)(ms_in / MSDAY);
  if (forceGMT) t.is_dst = false;
  int ms = (int)(ms_in - ((JsVarFloat)t.daysSinceEpoch * MSDAY));
  if (ms<0) {
    ms += MSDAY;
    t.daysSinceEpoch--;
  }
  int s = ms / 1000;
  t.ms = ms % 1000;
  t.hour = s / 3600;
  s = s % 3600;
  t.min = s/60;
  t.sec = s%60;
  return t;
}
JsVarFloat fromTimeInDay(TimeInDay *td) {
  return (JsVarFloat)(td->ms + (((td->hour*60+td->min - td->zone)*60+td->sec)*1000) + (JsVarFloat)td->daysSinceEpoch*MSDAY);
}
CalendarDate getCalendarDate(int d) {
  CalendarDate date;
  getDateFromDayNumber(d, &date.year, &date.month, &date.day);
  date.daysSinceEpoch = d;
  date.dow=(date.daysSinceEpoch+BASE_DOW)%7;
  if (date.dow<0) date.dow+=7;
  return date;
};
int fromCalendarDate(CalendarDate *date) {
  while (date->month < 0) {
    date->year--;
    date->month += 12;
  }
  while (date->month > 11) {
    date->year++;
    date->month -= 12;
  }
  return getDayNumberFromDate(date->year, date->month, date->day);
};
static int getMonth(const char *s) {
  int i;
  for (i=0;i<12;i++)
    if (s[0]==MONTHNAMES[i*4] &&
        s[1]==MONTHNAMES[i*4+1] &&
        s[2]==MONTHNAMES[i*4+2])
      return i;
  return -1;
}
static int getDay(const char *s) {
  int i;
  for (i=0;i<7;i++)
    if (strcmp(s, &DAYNAMES[i*4])==0)
      return i;
  return -1;
}
static TimeInDay getTimeFromDateVar(JsVar *date, bool forceGMT) {
  return getTimeFromMilliSeconds(jswrap_date_getTime(date), forceGMT);
}
static CalendarDate getCalendarDateFromDateVar(JsVar *date, bool forceGMT) {
  return getCalendarDate(getTimeFromDateVar(date, forceGMT).daysSinceEpoch);
}
JsVarFloat jswrap_date_now() {
  return ((JsVarFloat)jshGetSystemTime() * (1000.0 / (JsVarFloat)jshGetTimeFromMilliseconds(1000)));
}
JsVar *jswrap_date_from_milliseconds(JsVarFloat time) {
  JsVar *d = jspNewObject(0,"Date");
  jswrap_date_setTime(d, time);
  return d;
}
JsVar *jswrap_date_constructor(JsVar *args) {
  JsVarFloat time = 0;
  if (jsvGetArrayLength(args)==0) {
    time = jswrap_date_now();
  } else if (jsvGetArrayLength(args)==1) {
    JsVar *arg = jsvGetArrayItem(args, 0);
    if (jsvIsNumeric(arg))
      time = jsvGetFloat(arg);
    else if (jsvIsString(arg))
      time = jswrap_date_parse(arg);
    else
      jsExceptionHere(JSET_TYPEERROR, "Variables of type %t are not supported in date constructor", arg);
    jsvUnLock(arg);
  } else {
    CalendarDate date;
    date.year = (int)jsvGetIntegerAndUnLock(jsvGetArrayItem(args, 0));
    date.month = (int)(jsvGetIntegerAndUnLock(jsvGetArrayItem(args, 1)));
    date.day = (int)(jsvGetIntegerAndUnLock(jsvGetArrayItem(args, 2)));
    TimeInDay td;
    td.daysSinceEpoch = fromCalendarDate(&date);
    td.hour = (int)(jsvGetIntegerAndUnLock(jsvGetArrayItem(args, 3)));
    td.min = (int)(jsvGetIntegerAndUnLock(jsvGetArrayItem(args, 4)));
    td.sec = (int)(jsvGetIntegerAndUnLock(jsvGetArrayItem(args, 5)));
    td.ms = (int)(jsvGetIntegerAndUnLock(jsvGetArrayItem(args, 6)));
    setCorrectTimeZone(&td);
    time = fromTimeInDay(&td);
  }
  return jswrap_date_from_milliseconds(time);
}
int jswrap_date_getTimezoneOffset(JsVar *parent) {
  return -getTimeFromDateVar(parent, false ).zone;
}
int jswrap_date_getIsDST(JsVar *parent) {
  return getTimeFromDateVar(parent, false ).is_dst ? 1 : 0;
}
JsVarFloat jswrap_date_getTime(JsVar *date) {
  return jsvObjectGetFloatChild(date, "ms");
}
JsVarFloat jswrap_date_setTime(JsVar *date, JsVarFloat timeValue) {
  if (timeValue < -3.95083256832E+016 || timeValue >= 3.93840543168E+016) {
    jsExceptionHere(JSET_ERROR, "Date out of bounds");
    return 0.0;
  }
  if (date)
    jsvObjectSetChildAndUnLock(date, "ms", jsvNewFromFloat(timeValue));
  return timeValue;
}
int jswrap_date_getHours(JsVar *parent) {
  return getTimeFromDateVar(parent, false ).hour;
}
int jswrap_date_getMinutes(JsVar *parent) {
  return getTimeFromDateVar(parent, false ).min;
}
int jswrap_date_getSeconds(JsVar *parent) {
  return getTimeFromDateVar(parent, false ).sec;
}
int jswrap_date_getMilliseconds(JsVar *parent) {
  return getTimeFromDateVar(parent, false ).ms;
}
int jswrap_date_getDay(JsVar *parent) {
  return getCalendarDateFromDateVar(parent, false ).dow;
}
int jswrap_date_getDate(JsVar *parent) {
  return getCalendarDateFromDateVar(parent, false ).day;
}
int jswrap_date_getMonth(JsVar *parent) {
  return getCalendarDateFromDateVar(parent, false ).month;
}
int jswrap_date_getFullYear(JsVar *parent) {
  return getCalendarDateFromDateVar(parent, false ).year;
}
JsVarFloat jswrap_date_setHours(JsVar *parent, int hoursValue, JsVar *minutesValue, JsVar *secondsValue, JsVar *millisecondsValue) {
  TimeInDay td = getTimeFromDateVar(parent, false );
  td.hour = hoursValue;
  if (jsvIsNumeric(minutesValue))
    td.min = jsvGetInteger(minutesValue);
  if (jsvIsNumeric(secondsValue))
    td.sec = jsvGetInteger(secondsValue);
  if (jsvIsNumeric(millisecondsValue))
    td.ms = jsvGetInteger(millisecondsValue);
  setCorrectTimeZone(&td);
  return jswrap_date_setTime(parent, fromTimeInDay(&td));
}
JsVarFloat jswrap_date_setMinutes(JsVar *parent, int minutesValue, JsVar *secondsValue, JsVar *millisecondsValue) {
  TimeInDay td = getTimeFromDateVar(parent, false );
  td.min = minutesValue;
  if (jsvIsNumeric(secondsValue))
    td.sec = jsvGetInteger(secondsValue);
  if (jsvIsNumeric(millisecondsValue))
    td.ms = jsvGetInteger(millisecondsValue);
  setCorrectTimeZone(&td);
  return jswrap_date_setTime(parent, fromTimeInDay(&td));
}
JsVarFloat jswrap_date_setSeconds(JsVar *parent, int secondsValue, JsVar *millisecondsValue) {
  TimeInDay td = getTimeFromDateVar(parent, false );
  td.sec = secondsValue;
  if (jsvIsNumeric(millisecondsValue))
    td.ms = jsvGetInteger(millisecondsValue);
  setCorrectTimeZone(&td);
  return jswrap_date_setTime(parent, fromTimeInDay(&td));
}
JsVarFloat jswrap_date_setMilliseconds(JsVar *parent, int millisecondsValue) {
  TimeInDay td = getTimeFromDateVar(parent, false );
  td.ms = millisecondsValue;
  setCorrectTimeZone(&td);
  return jswrap_date_setTime(parent, fromTimeInDay(&td));
}
JsVarFloat jswrap_date_setDate(JsVar *parent, int dayValue) {
  TimeInDay td = getTimeFromDateVar(parent, false );
  CalendarDate d = getCalendarDate(td.daysSinceEpoch);
  d.day = dayValue;
  td.daysSinceEpoch = fromCalendarDate(&d);
  setCorrectTimeZone(&td);
  return jswrap_date_setTime(parent, fromTimeInDay(&td));
}
JsVarFloat jswrap_date_setMonth(JsVar *parent, int monthValue, JsVar *dayValue) {
  TimeInDay td = getTimeFromDateVar(parent, false );
  CalendarDate d = getCalendarDate(td.daysSinceEpoch);
  d.month = monthValue;
  if (jsvIsNumeric(dayValue))
    d.day = jsvGetInteger(dayValue);
  td.daysSinceEpoch = fromCalendarDate(&d);
  setCorrectTimeZone(&td);
  return jswrap_date_setTime(parent, fromTimeInDay(&td));
}
JsVarFloat jswrap_date_setFullYear(JsVar *parent, int yearValue, JsVar *monthValue, JsVar *dayValue) {
  TimeInDay td = getTimeFromDateVar(parent, false );
  CalendarDate d = getCalendarDate(td.daysSinceEpoch);
  d.year = yearValue;
  if (jsvIsNumeric(monthValue))
    d.month = jsvGetInteger(monthValue);
  if (jsvIsNumeric(dayValue))
    d.day = jsvGetInteger(dayValue);
  td.daysSinceEpoch = fromCalendarDate(&d);
  setCorrectTimeZone(&td);
  return jswrap_date_setTime(parent, fromTimeInDay(&td));
}
JsVar *jswrap_date_toString(JsVar *parent) {
  TimeInDay time = getTimeFromDateVar(parent, false );
  CalendarDate date = getCalendarDate(time.daysSinceEpoch);
  char zonesign;
  int zone;
  if (time.zone<0) {
    zone = -time.zone;
    zonesign = '-';
  } else {
    zone = +time.zone;
    zonesign = '+';
  }
  return jsvVarPrintf("%s %s %d %d %02d:%02d:%02d GMT%c%04d",
      &DAYNAMES[date.dow*4], &MONTHNAMES[date.month*4], date.day, date.year,
      time.hour, time.min, time.sec,
      zonesign, ((zone/60)*100)+(zone%60));
}
JsVar *jswrap_date_toUTCString(JsVar *parent) {
  TimeInDay time = getTimeFromDateVar(parent, true );
  CalendarDate date = getCalendarDate(time.daysSinceEpoch);
  return jsvVarPrintf("%s, %d %s %d %02d:%02d:%02d GMT", &DAYNAMES[date.dow*4], date.day, &MONTHNAMES[date.month*4], date.year, time.hour, time.min, time.sec);
}
JsVar *jswrap_date_toISOString(JsVar *parent) {
  TimeInDay time = getTimeFromDateVar(parent, true );
  CalendarDate date = getCalendarDate(time.daysSinceEpoch);
  return jsvVarPrintf("%d-%02d-%02dT%02d:%02d:%02d.%03dZ", date.year, date.month+1, date.day, time.hour, time.min, time.sec, time.ms);
}
JsVar *jswrap_date_toLocalISOString(JsVar *parent) {
  TimeInDay time = getTimeFromDateVar(parent, false );
  CalendarDate date = getCalendarDate(time.daysSinceEpoch);
  char zonesign;
  int zone;
  if (time.zone<0) {
    zone = -time.zone;
    zonesign = '-';
  } else {
    zone = +time.zone;
    zonesign = '+';
  }
  zone = 100*(zone/60) + (zone%60);
  return jsvVarPrintf("%d-%02d-%02dT%02d:%02d:%02d.%03d%c%04d", date.year, date.month+1, date.day, time.hour, time.min, time.sec, time.ms, zonesign, zone);
}
static JsVarInt _parse_int() {
  return (int)stringToIntWithRadix(jslGetTokenValueAsString(), 10, NULL, NULL);
}
static bool _parse_time(TimeInDay *time, int initialChars) {
  time->hour = (int)stringToIntWithRadix(&jslGetTokenValueAsString()[initialChars], 10, NULL, NULL);
  jslGetNextToken();
  if (lex->tk==':') {
    jslGetNextToken();
    if (lex->tk == LEX_INT) {
      time->min = _parse_int();
      jslGetNextToken();
      if (lex->tk==':') {
        jslGetNextToken();
        if (lex->tk == LEX_INT || lex->tk == LEX_FLOAT) {
          JsVarFloat f = stringToFloat(jslGetTokenValueAsString());
          time->sec = (int)f;
          time->ms = (int)(f*1000) % 1000;
          jslGetNextToken();
          if (lex->tk == LEX_ID) {
            const char *tkstr = jslGetTokenValueAsString();
            if (strcmp(tkstr,"GMT")==0 || strcmp(tkstr,"Z")==0) {
              time->zone = 0;
              jslGetNextToken();
              if (lex->tk == LEX_EOF) return true;
            } else {
              setCorrectTimeZone(time);
            }
          }
          if (lex->tk == '+' || lex->tk == '-') {
            int sign = lex->tk == '+' ? 1 : -1;
            jslGetNextToken();
            if (lex->tk == LEX_INT) {
              int i = _parse_int();
              i = (i%100) + ((i/100)*60);
              time->zone = i*sign;
              jslGetNextToken();
            } else {
              setCorrectTimeZone(time);
            }
          } else {
            setCorrectTimeZone(time);
          }
          return true;
        }
      }
    }
  }
  setCorrectTimeZone(time);
  return false;
}
JsVarFloat jswrap_date_parse(JsVar *str) {
  if (!jsvIsString(str)) return 0;
  TimeInDay time;
  time.daysSinceEpoch = 0;
  time.hour = 0;
  time.min = 0;
  time.sec = 0;
  time.ms = 0;
  time.zone = 0;
  time.is_dst = false;
  CalendarDate date = getCalendarDate(0);
  bool timezoneSet = false;
  JsLex lex;
  JsLex *oldLex = jslSetLex(&lex);
  jslInit(str);
  if (lex.tk == LEX_ID) {
    date.month = getMonth(jslGetTokenValueAsString());
    date.dow = getDay(jslGetTokenValueAsString());
    if (date.month>=0) {
      jslGetNextToken();
      if (lex.tk == LEX_INT) {
        date.day = _parse_int();
        jslGetNextToken();
        if (lex.tk==',') {
          jslGetNextToken();
          if (lex.tk == LEX_INT) {
            date.year = _parse_int();
            time.daysSinceEpoch = fromCalendarDate(&date);
            jslGetNextToken();
            if (lex.tk == LEX_INT) {
              _parse_time(&time, 0);
              timezoneSet = true;
            }
          }
        }
      }
    } else if (date.dow>=0) {
      date.month = 0;
      jslGetNextToken();
      if (lex.tk==',') {
        jslGetNextToken();
        if (lex.tk == LEX_INT) {
          date.day = _parse_int();
          jslGetNextToken();
          if (lex.tk == LEX_ID && getMonth(jslGetTokenValueAsString())>=0) {
            date.month = getMonth(jslGetTokenValueAsString());
            jslGetNextToken();
            if (lex.tk == LEX_INT) {
              date.year = _parse_int();
              time.daysSinceEpoch = fromCalendarDate(&date);
              jslGetNextToken();
              if (lex.tk == LEX_INT) {
                _parse_time(&time, 0);
                timezoneSet = true;
              }
            }
          }
        }
      }
    } else {
      date.dow = 0;
      date.month = 0;
    }
  } else if (lex.tk == LEX_INT) {
    date.year = _parse_int();
    jslGetNextToken();
    if (lex.tk=='-') {
      jslGetNextToken();
      if (lex.tk == LEX_INT) {
        date.month = _parse_int() - 1;
        jslGetNextToken();
        if (lex.tk=='-') {
          jslGetNextToken();
          if (lex.tk == LEX_INT) {
            date.day = _parse_int();
            time.daysSinceEpoch = fromCalendarDate(&date);
            jslGetNextToken();
            if (lex.tk == LEX_ID && jslGetTokenValueAsString()[0]=='T') {
              _parse_time(&time, 1);
              timezoneSet = true;
            }
          }
        }
      }
    }
  }
  if (!timezoneSet) setCorrectTimeZone(&time);
  jslKill();
  jslSetLex(oldLex);
  return fromTimeInDay(&time);
}
JsVar *_jswrap_error_constructor(JsVar *msg, char *type) {
  JsVar *d = jspNewObject(0,type);
  if (!d) return 0;
  if (msg)
    jsvObjectSetChildAndUnLock(d, "message", jsvAsString(msg));
  jsvObjectSetChildAndUnLock(d, "type", jsvNewFromString(type));
  if (lex) {
    JsVar *stackTrace = jsvNewFromEmptyString();
    if (stackTrace) {
      jspAppendStackTrace(stackTrace, lex);
      jsvObjectSetChildAndUnLock(d, "stack", stackTrace);
    }
  }
  return d;
}
JsVar *jswrap_error_constructor(JsVar *msg) {
  return _jswrap_error_constructor(msg, "Error");
}
JsVar *jswrap_syntaxerror_constructor(JsVar *msg) {
  return _jswrap_error_constructor(msg, "SyntaxError");
}
JsVar *jswrap_typeerror_constructor(JsVar *msg) {
  return _jswrap_error_constructor(msg, "TypeError");
}
JsVar *jswrap_internalerror_constructor(JsVar *msg) {
  return _jswrap_error_constructor(msg, "InternalError");
}
JsVar *jswrap_referenceerror_constructor(JsVar *msg) {
  return _jswrap_error_constructor(msg, "ReferenceError");
}
JsVar *jswrap_error_toString(JsVar *parent) {
  JsVar *str = jsvObjectGetChildIfExists(parent, "type");
  if (!str) str = jsvNewFromString("Error");
  if (!str) return 0;
  JsVar *msg = jsvObjectGetChildIfExists(parent, "message");
  if (msg) {
    JsVar *newStr = jsvVarPrintf("%v: %v", str, msg);
    jsvUnLock2(msg, str);
    str = newStr;
  }
  return str;
}
JsVar *jswrap_global() {
  return jsvLockAgain(execInfo.root);
}
extern JsExecInfo execInfo;
JsVar *jswrap_arguments() {
  JsVar *scope = 0;
  if (execInfo.baseScope)
    scope = jsvLockAgain(execInfo.baseScope);
  if (!jsvIsFunction(scope)) {
    jsExceptionHere(JSET_ERROR, "Can only use 'arguments' variable inside a function");
    return 0;
  }
  JsVar *result = jsvGetFunctionArgumentLength(scope);
  jsvObjectSetChild(scope,"arguments",result);
  jsvUnLock(scope);
  return result;
}
JsVar *jswrap_function_constructor(JsVar *args) {
  JsVar *fn = jsvNewWithFlags(JSV_FUNCTION);
  if (!fn) return 0;
  JsvObjectIterator it;
  jsvObjectIteratorNew(&it, args);
  JsVar *v = jsvObjectIteratorGetValue(&it);
  jsvObjectIteratorNext(&it);
  while (jsvObjectIteratorHasValue(&it)) {
    JsVar *s = jsvAsString(v);
    if (s) {
      JsVar *paramName = jsvNewFromString("\xFF");
      if (paramName) {
        jsvAppendStringVarComplete(paramName, s);
        jsvAddFunctionParameter(fn, paramName, 0);
      }
      jsvUnLock(s);
    }
    jsvUnLock(v);
    v = jsvObjectIteratorGetValue(&it);
    jsvObjectIteratorNext(&it);
  }
  jsvObjectIteratorFree(&it);
  if (!jsvIsString(v)) {
    jsExceptionHere(JSET_TYPEERROR,"Function code must be a String, got '%t'", v);
    jsvUnLock2(v,fn);
    return NULL;
  }
  jsvObjectSetChildAndUnLock(fn, "\xFF""cod", v);
  return fn;
}
JsVar *jswrap_eval(JsVar *v) {
  if (!v) return 0;
  JsVar *s = jsvAsString(v);
  JsVar *result = jspEvaluateVar(s, 0, "eval");
  jsvUnLock(s);
  return result;
}
JsVar *jswrap_parseInt(JsVar *v, JsVar *radixVar) {
  int radix = 0;
  if (jsvIsNumeric(radixVar))
    radix = (int)jsvGetInteger(radixVar);
  if (jsvIsFloat(v) && !isfinite(jsvGetFloat(v)))
    return jsvNewFromFloat(NAN);
  char buffer[70];
  char *bufferStart = buffer;
  jsvGetString(v, buffer, 70);
  bool hasError = false;
  if (((!radix) || (radix==16)) &&
      buffer[0]=='0' && (buffer[1]=='x' || buffer[1]=='X')) {
    radix = 16;
    bufferStart += 2;
  }
  if (!radix) {
    radix = 10;
  }
  const char *endOfInteger;
  long long i = stringToIntWithRadix(bufferStart, radix, &hasError, &endOfInteger);
  if (hasError) return jsvNewFromFloat(NAN);
  if (endOfInteger == &buffer[sizeof(buffer)-1]) {
    jsExceptionHere(JSET_ERROR, "String too big to convert to number");
    return jsvNewFromFloat(NAN);
  }
  return jsvNewFromLongInteger(i);
}
JsVarFloat jswrap_parseFloat(JsVar *v) {
  char buffer[70];
  jsvGetString(v, buffer, 70);
  if (!strcmp(buffer, "Infinity")) return INFINITY;
  if (!strcmp(buffer, "-Infinity")) return -INFINITY;
  const char *endOfFloat;
  JsVarFloat f = stringToFloatWithRadix(buffer,0,&endOfFloat);
  if (endOfFloat == &buffer[sizeof(buffer)-1]) {
    jsExceptionHere(JSET_ERROR, "String too big to convert to number");
    return NAN;
  }
  return f;
}
bool jswrap_isFinite(JsVar *v) {
  JsVarFloat f = jsvGetFloat(v);
  return !isnan(f) && f!=INFINITY && f!=-INFINITY;
}
bool jswrap_isNaN(JsVar *v) {
  if (jsvIsUndefined(v) ||
      jsvIsObject(v) ||
      ((jsvIsFloat(v)||jsvIsArray(v)) && isnan(jsvGetFloat(v)))) return true;
  if (jsvIsString(v)) {
    bool allWhiteSpace = true;
    JsvStringIterator it;
    jsvStringIteratorNew(&it,v,0);
    while (jsvStringIteratorHasChar(&it)) {
      if (!isWhitespace(jsvStringIteratorGetCharAndNext(&it))) {
        allWhiteSpace = false;
        break;
      }
    }
    jsvStringIteratorFree(&it);
    if (allWhiteSpace) return false;
    return isnan(jsvGetFloat(v));
  }
  return false;
}
__attribute__ ((noinline)) static int jswrap_btoa_encode(int c) {
  c = c & 0x3F;
  if (c<26) return 'A'+c;
  if (c<52) return 'a'+c-26;
  if (c<62) return '0'+c-52;
  if (c==62) return '+';
  return '/';
}
__attribute__ ((noinline)) static int jswrap_atob_decode(int c) {
  c = c & 0xFF;
  if (c>='A' && c<='Z') return c-'A';
  if (c>='a' && c<='z') return 26+c-'a';
  if (c>='0' && c<='9') return 52+c-'0';
  if (c=='+') return 62;
  if (c=='/') return 63;
  return -1;
}
JsVar *jswrap_btoa(JsVar *binaryData) {
  if (!jsvIsIterable(binaryData)) {
    jsExceptionHere(JSET_ERROR, "Expecting String or Array, got %t", binaryData);
    return 0;
  }
  size_t inputLength = (size_t)jsvGetLength(binaryData);
  size_t outputLength = ((inputLength+2)/3)*4;
  JsVar* base64Data = jsvNewStringOfLength((unsigned int)outputLength, NULL);
  if (!base64Data) return 0;
  JsvIterator itsrc;
  JsvStringIterator itdst;
  jsvIteratorNew(&itsrc, binaryData, JSIF_EVERY_ARRAY_ELEMENT);
  jsvStringIteratorNew(&itdst, base64Data, 0);
  int padding = 0;
  while (jsvIteratorHasElement(&itsrc) && !jspIsInterrupted()) {
    int octet_a = (unsigned char)jsvIteratorGetIntegerValue(&itsrc)&255;
    jsvIteratorNext(&itsrc);
    int octet_b = 0, octet_c = 0;
    if (jsvIteratorHasElement(&itsrc)) {
      octet_b = jsvIteratorGetIntegerValue(&itsrc)&255;
      jsvIteratorNext(&itsrc);
      if (jsvIteratorHasElement(&itsrc)) {
        octet_c = jsvIteratorGetIntegerValue(&itsrc)&255;
        jsvIteratorNext(&itsrc);
        padding = 0;
      } else
        padding = 1;
    } else
      padding = 2;
    int triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;
    jsvStringIteratorSetCharAndNext(&itdst, (char)jswrap_btoa_encode(triple >> 18));
    jsvStringIteratorSetCharAndNext(&itdst, (char)jswrap_btoa_encode(triple >> 12));
    jsvStringIteratorSetCharAndNext(&itdst, (char)((padding>1)?'=':jswrap_btoa_encode(triple >> 6)));
    jsvStringIteratorSetCharAndNext(&itdst, (char)((padding>0)?'=':jswrap_btoa_encode(triple)));
  }
  jsvIteratorFree(&itsrc);
  jsvStringIteratorFree(&itdst);
  return base64Data;
}
JsVar *jswrap_atob(JsVar *base64Data) {
  if (!jsvIsString(base64Data)) {
    jsExceptionHere(JSET_ERROR, "Expecting String, got %t", base64Data);
    return 0;
  }
  size_t inputLength = 0;
  JsvStringIterator itsrc;
  jsvStringIteratorNew(&itsrc, base64Data, 0);
  char prevCh = 0, prevPrevCh = 0;
  while (jsvStringIteratorHasChar(&itsrc)) {
    char ch = jsvStringIteratorGetChar(&itsrc);
    if (!isWhitespace(ch)) {
      prevPrevCh = prevCh;
      prevCh = ch;
      inputLength++;
    }
    jsvStringIteratorNext(&itsrc);
  }
  jsvStringIteratorFree(&itsrc);
  size_t outputLength = inputLength*3/4;
  if (prevCh=='=') outputLength--;
  if (prevPrevCh=='=') outputLength--;
  JsVar* binaryData = jsvNewStringOfLength((unsigned int)outputLength, NULL);
  if (!binaryData) return 0;
  JsvStringIterator itdst;
  jsvStringIteratorNew(&itsrc, base64Data, 0);
  jsvStringIteratorNew(&itdst, binaryData, 0);
  while (jsvStringIteratorHasChar(&itsrc) && !jspIsInterrupted()) {
    uint32_t triple = 0;
    int i, valid=0;
    for (i=0;i<4;i++) {
      if (jsvStringIteratorHasChar(&itsrc)) {
        char ch = ' ';
        while (ch && isWhitespace(ch)) ch=jsvStringIteratorGetCharAndNext(&itsrc);
        int sextet = jswrap_atob_decode(ch);
        if (sextet>=0) {
          triple |= (unsigned int)(sextet) << ((3-i)*6);
          valid=i;
        }
      }
    }
    if (valid>0) jsvStringIteratorSetCharAndNext(&itdst, (char)(triple >> 16));
    if (valid>1) jsvStringIteratorSetCharAndNext(&itdst, (char)(triple >> 8));
    if (valid>2) jsvStringIteratorSetCharAndNext(&itdst, (char)(triple));
  }
  jsvStringIteratorFree(&itsrc);
  jsvStringIteratorFree(&itdst);
  return binaryData;
}
JsVar *jswrap_encodeURIComponent(JsVar *arg) {
  JsVar *v = jsvAsString(arg);
  if (!v) return 0;
  JsVar *result = jsvNewFromEmptyString();
  if (result) {
    JsvStringIterator it;
    jsvStringIteratorNew(&it, v, 0);
    JsvStringIterator dst;
    jsvStringIteratorNew(&dst, result, 0);
    while (jsvStringIteratorHasChar(&it)) {
      char ch = jsvStringIteratorGetCharAndNext(&it);
      if (isAlpha(ch) || isNumeric(ch) ||
          ch=='-' ||
          ch=='.' ||
          ch=='!' ||
          ch=='~' ||
          ch=='*' ||
          ch=='\'' ||
          ch=='(' ||
          ch==')') {
        jsvStringIteratorAppend(&dst, ch);
      } else {
        jsvStringIteratorAppend(&dst, '%');
        unsigned int d = ((unsigned)ch)>>4;
        jsvStringIteratorAppend(&dst, (char)((d>9)?('A'+d-10):('0'+d)));
        d = ((unsigned)ch)&15;
        jsvStringIteratorAppend(&dst, (char)((d>9)?('A'+d-10):('0'+d)));
      }
    }
    jsvStringIteratorFree(&dst);
    jsvStringIteratorFree(&it);
  }
  jsvUnLock(v);
  return result;
}
JsVar *jswrap_decodeURIComponent(JsVar *arg) {
  JsVar *v = jsvAsString(arg);
  if (!v) return 0;
  JsVar *result = jsvNewFromEmptyString();
  if (result) {
    JsvStringIterator it;
    jsvStringIteratorNew(&it, v, 0);
    JsvStringIterator dst;
    jsvStringIteratorNew(&dst, result, 0);
    while (jsvStringIteratorHasChar(&it)) {
      char ch = jsvStringIteratorGetCharAndNext(&it);
      if (ch>>7) {
        jsExceptionHere(JSET_ERROR, "ASCII only");
        break;
      }
      if (ch!='%') {
        jsvStringIteratorAppend(&dst, ch);
      } else {
        char hi = (char)jsvStringIteratorGetCharAndNext(&it);
        char lo = (char)jsvStringIteratorGetCharAndNext(&it);
        int v = (char)hexToByte(hi,lo);
        if (v<0) {
          jsExceptionHere(JSET_ERROR, "Invalid URI");
          break;
        }
        ch = (char)v;
        jsvStringIteratorAppend(&dst, ch);
      }
    }
    jsvStringIteratorFree(&dst);
    jsvStringIteratorFree(&it);
  }
  jsvUnLock(v);
  return result;
}
void jswrap_trace(JsVar *root) {
  if (jsvIsUndefined(root)) {
    jsvTrace(execInfo.root, 0);
  } else {
    jsvTrace(root, 0);
  }
}
void jswrap_print(JsVar *v) {
  do { if (!(jsvIsArray(v))) jsAssertFail("bin/espruino_embedded.c",19016,""); } while(0);
  jsiConsoleRemoveInputLine();
  JsvObjectIterator it;
  jsvObjectIteratorNew(&it, v);
  while (jsvObjectIteratorHasValue(&it)) {
    JsVar *v = jsvObjectIteratorGetValue(&it);
    if (jsvIsString(v))
      jsiConsolePrintStringVar(v);
    else
      jsfPrintJSON(v, JSON_PRETTY | JSON_SOME_NEWLINES | JSON_SHOW_OBJECT_NAMES);
    jsvUnLock(v);
    jsvObjectIteratorNext(&it);
    if (jsvObjectIteratorHasValue(&it))
      jsiConsolePrintString(" ");
  }
  jsvObjectIteratorFree(&it);
  jsiConsolePrintString("\n");
}
void jswrap_console_trace(JsVar *v) {
  if (v) jswrap_print(v);
  jslPrintStackTrace(vcbprintf_callback_jsiConsolePrintString, NULL, lex);
}
const unsigned int JSON_LIMIT_AMOUNT = 15;
const unsigned int JSON_LIMITED_AMOUNT = 5;
const unsigned int JSON_LIMIT_STRING_AMOUNT = 60;
const unsigned int JSON_LIMITED_STRING_AMOUNT = 27;
const unsigned int JSON_ITEMS_ON_LINE_OBJECT = 4;
const char *JSON_LIMIT_TEXT = " ... ";
JsVar *jswrap_json_stringify(JsVar *v, JsVar *replacer, JsVar *space) {
  ( (void)(replacer) );
  JSONFlags flags = JSON_IGNORE_FUNCTIONS|JSON_NO_UNDEFINED|JSON_ARRAYBUFFER_AS_ARRAY|JSON_JSON_COMPATIBILE|JSON_ALLOW_TOJSON;
  JsVar *result = jsvNewFromEmptyString();
  if (result) {
    char whitespace[11] = "";
    if (jsvIsUndefined(space) || jsvIsNull(space)) {
    } else if (jsvIsNumeric(space)) {
      int s = (int)jsvGetInteger(space);
      if (s<0) s=0;
      if (s>10) s=10;
      whitespace[s] = 0;
      while (s) whitespace[--s]=' ';
    } else {
      jsvGetString(space, whitespace, sizeof(whitespace)-1);
    }
    if (strlen(whitespace)) flags |= JSON_ALL_NEWLINES|JSON_PRETTY;
    jsfGetJSONWhitespace(v, result, flags, whitespace);
  }
  return result;
}
JsVar *jswrap_json_parse_internal(JSONFlags flags) {
  switch (lex->tk) {
  case LEX_R_TRUE: jslGetNextToken(); return jsvNewFromBool(true);
  case LEX_R_FALSE: jslGetNextToken(); return jsvNewFromBool(false);
  case LEX_R_NULL: jslGetNextToken(); return jsvNewWithFlags(JSV_NULL);
  case '-': {
    jslGetNextToken();
    if (lex->tk!=LEX_INT && lex->tk!=LEX_FLOAT) return 0;
    JsVar *v = jswrap_json_parse_internal(flags);
    JsVar *zero = jsvNewFromInteger(0);
    JsVar *r = jsvMathsOp(zero, v, '-');
    jsvUnLock2(v, zero);
    return r;
  }
  case LEX_INT: {
    JsVar *v = jslGetTokenValueAsVar();
    jslGetNextToken();
    return v;
  }
  case LEX_FLOAT: {
    JsVarFloat v = stringToFloat(jslGetTokenValueAsString());
    jslGetNextToken();
    return jsvNewFromFloat(v);
  }
  case LEX_STR: {
    JsVar *a = jslGetTokenValueAsVar();
    jslGetNextToken();
    return a;
  }
  case '[': {
    JsVar *arr = jsvNewEmptyArray(); if (!arr) return 0;
    jslGetNextToken();
    while (lex->tk != ']' && !jspHasError()) {
      JsVar *value = jswrap_json_parse_internal(flags);
      if (!value ||
          (lex->tk!=']' && !jslMatch(','))) {
        jsvUnLock2(value, arr);
        return 0;
      }
      jsvArrayPush(arr, value);
      jsvUnLock(value);
    }
    if (!jslMatch(']')) {
      jsvUnLock(arr);
      return 0;
    }
    return arr;
  }
  case '{': {
    JsVar *obj = jsvNewObject(); if (!obj) return 0;
    jslGetNextToken();
    while ((lex->tk == LEX_STR || lex->tk == LEX_INT || jslIsIDOrReservedWord()) && !jspHasError()) {
      if (!(flags&JSON_DROP_QUOTES) && (jslIsIDOrReservedWord() || lex->tk == LEX_INT)) {
       jslMatch(LEX_STR);
        return obj;
      }
      JsVar *key = jsvAsArrayIndexAndUnLock(jslGetTokenValueAsVar());
      jslGetNextToken();
      JsVar *value = 0;
      if (!jslMatch(':') ||
          !(value=jswrap_json_parse_internal(flags)) ||
          (lex->tk!='}' && !jslMatch(','))) {
        jsvUnLock3(key, value, obj);
        return 0;
      }
      jsvAddName(obj, jsvMakeIntoVariableName(key, value));
      jsvUnLock2(value, key);
    }
    if (!jslMatch('}')) {
      jsvUnLock(obj);
      return 0;
    }
    return obj;
  }
  default: {
    char buf[32];
    jslTokenAsString(lex->tk, buf, 32);
    jsExceptionHere(JSET_SYNTAXERROR, "Expecting valid value, got %s", buf);
    return 0;
  }
  }
}
JsVar *jswrap_json_parse_ext(JsVar *v, JSONFlags flags) {
  JsLex lex;
  JsVar *str = jsvAsString(v);
  JsLex *oldLex = jslSetLex(&lex);
  jslInit(str);
  jsvUnLock(str);
  JsVar *res = jswrap_json_parse_internal(flags);
  jslKill();
  jslSetLex(oldLex);
  return res;
}
JsVar *jswrap_json_parse(JsVar *v) {
  return jswrap_json_parse_ext(v, 0);
}
JsVar *jswrap_json_parse_liberal(JsVar *v, bool noExceptions) {
  JsVar *res = jswrap_json_parse_ext(v, JSON_DROP_QUOTES);
  if (noExceptions) {
    jsvUnLock(jspGetException());
    execInfo.execute &= (JsExecFlags)~EXEC_EXCEPTION;
  }
  return res;
}
void jsfGetJSONForFunctionWithCallback(JsVar *var, JSONFlags flags, vcbprintf_callback user_callback, void *user_data) {
  do { if (!(jsvIsFunction(var))) jsAssertFail("bin/espruino_embedded.c",19304,""); } while(0);
  JsVar *codeVar = 0;
  JsvObjectIterator it;
  jsvObjectIteratorNew(&it, var);
  bool firstParm = true;
  cbprintf(user_callback, user_data, "(");
  while (jsvObjectIteratorHasValue(&it)) {
    JsVar *child = jsvObjectIteratorGetKey(&it);
    if (jsvIsFunctionParameter(child)) {
      if (firstParm)
        firstParm=false;
      else
        cbprintf(user_callback, user_data, ",");
      JsVar *name = jsvNewFromStringVar(child, 1, (0x7FFFFFFF));
      cbprintf(user_callback, user_data, "%v", name);
      jsvUnLock(name);
    } else if (jsvIsString(child) && jsvIsStringEqual(child, "\xFF""cod")) {
      codeVar = jsvObjectIteratorGetValue(&it);
    }
    jsvUnLock(child);
    jsvObjectIteratorNext(&it);
  }
  jsvObjectIteratorFree(&it);
  cbprintf(user_callback, user_data, ") ");
  if (jsvIsNativeFunction(var)) {
    cbprintf(user_callback, user_data, "{ [native code] }");
  } else {
    if (codeVar) {
      if (flags & JSON_LIMIT) {
        cbprintf(user_callback, user_data, "{%s}", JSON_LIMIT_TEXT);
      } else {
        bool hasNewLine = jsvGetStringIndexOf(codeVar,'\n')>=0;
        user_callback(hasNewLine?"{\n  ":"{", user_data);
        if (jsvIsFunctionReturn(var))
          user_callback("return ", user_data);
        jslPrintTokenisedString(codeVar, user_callback, user_data);
        user_callback(hasNewLine?"\n}":"}", user_data);
      }
    } else cbprintf(user_callback, user_data, "{}");
  }
  jsvUnLock(codeVar);
}
bool jsonNeedsNewLine(JsVar *v) {
  return !(jsvIsUndefined(v) || jsvIsNull(v) || jsvIsNumeric(v));
}
void jsonNewLine(JSONFlags flags, const char *whitespace, vcbprintf_callback user_callback, void *user_data) {
  user_callback("\n", user_data);
  unsigned int indent = flags / JSON_INDENT;
  while (indent--)
    user_callback(whitespace, user_data);
}
static bool jsfGetJSONForObjectItWithCallback(JsvObjectIterator *it, JSONFlags flags, const char *whitespace, JSONFlags nflags, vcbprintf_callback user_callback, void *user_data, bool first) {
  bool needNewLine = false;
  size_t sinceNewLine = 0;
  while (jsvObjectIteratorHasValue(it) && !jspIsInterrupted()) {
    JsVar *index = jsvObjectIteratorGetKey(it);
    JsVar *item = jsvGetValueOfName(index);
    bool hidden = jsvIsInternalObjectKey(index) ||
        ((flags & JSON_IGNORE_FUNCTIONS) && jsvIsFunction(item)) ||
        ((flags&JSON_NO_UNDEFINED) && jsvIsUndefined(item)) ||
        jsvIsGetterOrSetter(item);
    if (!hidden) {
      sinceNewLine++;
      if (!first) cbprintf(user_callback, user_data, (flags&JSON_PRETTY)?", ":",");
      bool newNeedsNewLine = (flags&JSON_SOME_NEWLINES) && jsonNeedsNewLine(item);
      if ((flags&JSON_SOME_NEWLINES) && sinceNewLine>JSON_ITEMS_ON_LINE_OBJECT)
        needNewLine = true;
      if (flags&JSON_ALL_NEWLINES) {
        needNewLine = true;
        newNeedsNewLine = true;
      }
      if (needNewLine || newNeedsNewLine) {
        jsonNewLine(nflags, whitespace, user_callback, user_data);
        needNewLine = false;
        sinceNewLine = 0;
      }
      bool addQuotes = true;
      if (flags&JSON_DROP_QUOTES) {
        if (jsvIsIntegerish(index)) addQuotes = false;
        else if (jsvIsString(index) && jsvGetStringLength(index)<63) {
          char buf[64];
          jsvGetString(index,buf,sizeof(buf));
          if (isIDString(buf)) addQuotes=false;
        }
      }
      cbprintf(user_callback, user_data, addQuotes?((flags&JSON_ALL_UNICODE_ESCAPE)?"%Q%s":"%q%s"):"%v%s", index, (flags&JSON_PRETTY)?": ":":");
      if (first)
        first = false;
      jsfGetJSONWithCallback(item, index, nflags, whitespace, user_callback, user_data);
      needNewLine = newNeedsNewLine;
    }
    jsvUnLock2(index, item);
    jsvObjectIteratorNext(it);
  }
  return needNewLine;
}
void jsfGetJSONWithCallback(JsVar *var, JsVar *varName, JSONFlags flags, const char *whitespace, vcbprintf_callback user_callback, void *user_data) {
  JSONFlags nflags = flags + JSON_INDENT;
  if (!whitespace) whitespace="  ";
  if (jsvIsUndefined(var)) {
    cbprintf(user_callback, user_data, (flags&JSON_NO_UNDEFINED)?"null":"undefined");
    return;
  }
  if ((var->flags & JSV_IS_RECURSING) || (jsuGetFreeStack() < 512) || jspIsInterrupted()) {
    cbprintf(user_callback, user_data, " ... ");
    return;
  }
  var->flags |= JSV_IS_RECURSING;
  if (jsvIsArray(var)) {
    JsVarInt length = jsvGetArrayLength(var);
    bool limited = (flags&JSON_LIMIT) && (length>(JsVarInt)JSON_LIMIT_AMOUNT);
    bool needNewLine = false;
    cbprintf(user_callback, user_data, (flags&JSON_PRETTY)?"[ ":"[");
    JsVarInt lastIndex = -1;
    bool numeric = true;
    bool first = true;
    JsvObjectIterator it;
    jsvObjectIteratorNew(&it, var);
    while (lastIndex+1<length && numeric && !jspIsInterrupted()) {
      JsVar *key = jsvObjectIteratorGetKey(&it);
      if (!jsvObjectIteratorHasValue(&it) || jsvIsNumeric(key)) {
        JsVarInt index = jsvObjectIteratorHasValue(&it) ? jsvGetInteger(key) : length-1;
        JsVar *item = jsvObjectIteratorGetValue(&it);
        while (lastIndex < index) {
          lastIndex++;
          if (!limited || lastIndex<(JsVarInt)JSON_LIMITED_AMOUNT || lastIndex>=length-(JsVarInt)JSON_LIMITED_AMOUNT) {
            if (!first) cbprintf(user_callback, user_data, (flags&JSON_PRETTY)?", ":",");
            first = false;
            if (limited && lastIndex==length-(JsVarInt)JSON_LIMITED_AMOUNT) cbprintf(user_callback, user_data, JSON_LIMIT_TEXT);
            bool newNeedsNewLine = ((flags&JSON_SOME_NEWLINES) && jsonNeedsNewLine(item));
            if (flags&JSON_ALL_NEWLINES) {
              needNewLine = true;
              newNeedsNewLine = true;
            }
            if (needNewLine || newNeedsNewLine) {
              jsonNewLine(nflags, whitespace, user_callback, user_data);
              needNewLine = false;
            }
            if (lastIndex == index) {
              JsVar *indexVar = jsvNewFromInteger(index);
              jsfGetJSONWithCallback(item, indexVar, nflags, whitespace, user_callback, user_data);
              jsvUnLock(indexVar);
            } else
              cbprintf(user_callback, user_data, (flags&JSON_NO_UNDEFINED)?"null":"undefined");
            needNewLine = newNeedsNewLine;
          }
        }
        jsvUnLock(item);
        jsvObjectIteratorNext(&it);
      } else {
        numeric = false;
      }
      jsvUnLock(key);
    }
    if ((flags&JSON_PRETTY))
      jsfGetJSONForObjectItWithCallback(&it, flags, whitespace, nflags, user_callback, user_data, first);
    jsvObjectIteratorFree(&it);
    if (needNewLine) jsonNewLine(flags, whitespace, user_callback, user_data);
    cbprintf(user_callback, user_data, (flags&JSON_PRETTY)?" ]":"]");
  } else if (jsvIsArrayBuffer(var)) {
    JsvArrayBufferIterator it;
    bool allZero = true;
    jsvArrayBufferIteratorNew(&it, var, 0);
    while (jsvArrayBufferIteratorHasElement(&it)) {
      if (jsvArrayBufferIteratorGetFloatValue(&it)!=0)
        allZero = false;
      jsvArrayBufferIteratorNext(&it);
    }
    jsvArrayBufferIteratorFree(&it);
    bool asArray = flags&JSON_ARRAYBUFFER_AS_ARRAY;
    if (allZero && !asArray) {
      cbprintf(user_callback, user_data, "new %s(%d)", jswGetBasicObjectName(var), jsvGetArrayBufferLength(var));
    } else {
      const char *aname = jswGetBasicObjectName(var);
      bool isBasicArrayBuffer = strcmp(aname,"ArrayBuffer")==0;
      if (isBasicArrayBuffer) {
        aname="Uint8Array";
      }
      cbprintf(user_callback, user_data, asArray?"[":"new %s([", aname);
      if (flags&JSON_ALL_NEWLINES) jsonNewLine(nflags, whitespace, user_callback, user_data);
      size_t length = jsvGetArrayBufferLength(var);
      bool limited = (flags&JSON_LIMIT) && (length>JSON_LIMIT_AMOUNT);
      jsvArrayBufferIteratorNew(&it, var, 0);
      while (jsvArrayBufferIteratorHasElement(&it) && !jspIsInterrupted()) {
        if (!limited || it.index<JSON_LIMITED_AMOUNT || it.index>=length-JSON_LIMITED_AMOUNT) {
          if (it.index>0) cbprintf(user_callback, user_data, (flags&JSON_PRETTY)?", ":",");
          if (flags&JSON_ALL_NEWLINES) jsonNewLine(nflags, whitespace, user_callback, user_data);
          if (limited && it.index==length-JSON_LIMITED_AMOUNT) cbprintf(user_callback, user_data, JSON_LIMIT_TEXT);
          JsVar *item = jsvArrayBufferIteratorGetValue(&it, false );
          jsfGetJSONWithCallback(item, NULL, nflags, whitespace, user_callback, user_data);
          jsvUnLock(item);
        }
        jsvArrayBufferIteratorNext(&it);
      }
      if (flags&JSON_ALL_NEWLINES) jsonNewLine(flags, whitespace, user_callback, user_data);
      jsvArrayBufferIteratorFree(&it);
      cbprintf(user_callback, user_data, asArray?"]":"])");
      if (isBasicArrayBuffer && !asArray) cbprintf(user_callback, user_data, ".buffer");
    }
  } else if (jsvIsObject(var)) {
    {
      bool showContents = true;
      if (flags & JSON_SHOW_OBJECT_NAMES) {
        JsVar *proto = jsvObjectGetChildIfExists(var, "__proto__");
        if (jsvHasChildren(proto)) {
          JsVar *constr = jsvObjectGetChildIfExists(proto, "constructor");
          if (constr) {
            JsVar *p = jsvGetIndexOf(execInfo.root, constr, true);
            if (p) cbprintf(user_callback, user_data, "%v: ", p);
            jsvUnLock2(p,constr);
            JsVar *toStringFn = jspGetNamedField(var, "toString", false);
            if (jsvIsFunction(toStringFn) && toStringFn->varData.native.ptr != (void (*)(void))jswrap_object_toString) {
              JsVar *result = jspExecuteFunction(toStringFn,var,0,0);
              cbprintf(user_callback, user_data, "%v", result);
              jsvUnLock(result);
              showContents = false;
            }
            jsvUnLock(toStringFn);
          }
        }
        jsvUnLock(proto);
      }
      if (showContents) {
        JsVar *toStringFn = 0;
        if (flags & JSON_ALLOW_TOJSON)
          toStringFn = jspGetNamedField(var, "toJSON", false);
        if (jsvIsFunction(toStringFn)) {
          JsVar *varNameStr = varName ? jsvAsString(varName) : 0;
          JsVar *result = jspExecuteFunction(toStringFn,var,1,&varNameStr);
          jsvUnLock(varNameStr);
          if (result==var) var->flags &= ~JSV_IS_RECURSING;
          jsfGetJSONWithCallback(result, NULL, flags&~JSON_ALLOW_TOJSON, whitespace, user_callback, user_data);
          jsvUnLock(result);
        } else {
          JsvObjectIterator it;
          jsvObjectIteratorNew(&it, var);
          cbprintf(user_callback, user_data, (flags&JSON_PRETTY)?"{ ":"{");
          bool needNewLine = jsfGetJSONForObjectItWithCallback(&it, flags, whitespace, nflags, user_callback, user_data, true);
          jsvObjectIteratorFree(&it);
          if (needNewLine) jsonNewLine(flags, whitespace, user_callback, user_data);
          cbprintf(user_callback, user_data, (flags&JSON_PRETTY)?" }":"}");
        }
        jsvUnLock(toStringFn);
      }
    }
  } else if (jsvIsFunction(var)) {
    if (flags & JSON_IGNORE_FUNCTIONS) {
      cbprintf(user_callback, user_data, "undefined");
    } else {
      cbprintf(user_callback, user_data, "function ");
      jsfGetJSONForFunctionWithCallback(var, nflags, user_callback, user_data);
    }
  } else if ((jsvIsString(var) && !jsvIsName(var)) || ((flags&JSON_PIN_TO_STRING)&&jsvIsPin(var))) {
    if ((flags&JSON_LIMIT) && jsvGetStringLength(var)>JSON_LIMIT_STRING_AMOUNT) {
      JsVar *var1 = jsvNewFromStringVar(var, 0, JSON_LIMITED_STRING_AMOUNT);
      JsVar *var2 = jsvNewFromStringVar(var, jsvGetStringLength(var)-JSON_LIMITED_STRING_AMOUNT, JSON_LIMITED_STRING_AMOUNT);
      cbprintf(user_callback, user_data, "%q%s%q", var1, JSON_LIMIT_TEXT, var2);
      jsvUnLock2(var1, var2);
    } else {
      cbprintf(user_callback, user_data, (flags&JSON_ALL_UNICODE_ESCAPE)?"%Q":"%q", var);
    }
  } else if ((flags&JSON_NO_NAN) && jsvIsFloat(var) && !isfinite(jsvGetFloat(var))) {
    cbprintf(user_callback, user_data, "null");
  } else {
    cbprintf(user_callback, user_data, "%v", var);
  }
  var->flags &= ~JSV_IS_RECURSING;
}
void jsfGetJSONWhitespace(JsVar *var, JsVar *result, JSONFlags flags, const char *whitespace) {
  do { if (!(jsvIsString(result))) jsAssertFail("bin/espruino_embedded.c",19614,""); } while(0);
  JsvStringIterator it;
  jsvStringIteratorNew(&it, result, 0);
  jsvStringIteratorGotoEnd(&it);
  jsfGetJSONWithCallback(var, NULL, flags, whitespace, (vcbprintf_callback)&jsvStringIteratorPrintfCallback, &it);
  jsvStringIteratorFree(&it);
}
void jsfGetJSON(JsVar *var, JsVar *result, JSONFlags flags) {
  jsfGetJSONWhitespace(var, result, flags, 0);
}
void jsfPrintJSON(JsVar *var, JSONFlags flags) {
  jsfGetJSONWithCallback(var, NULL, flags, 0, vcbprintf_callback_jsiConsolePrintString, 0);
}
void jsfPrintJSONForFunction(JsVar *var, JSONFlags flags) {
  jsfGetJSONForFunctionWithCallback(var, flags, vcbprintf_callback_jsiConsolePrintString, 0);
}
JsVar *jswrap_number_constructor(JsVar *args) {
  if (jsvGetArrayLength(args)==0) return jsvNewFromInteger(0);
  JsVar *val = jsvGetArrayItem(args, 0);
  JsVar *result = 0;
  if (jsvIsArray(val)) {
    JsVarInt l = jsvGetArrayLength(val);
    if (l==0) result = jsvNewFromInteger(0);
    else if (l==1) {
      JsVar *n = jsvGetArrayItem(val, 0);
      if (jsvIsString(n) && jsvIsEmptyString(n)) result = jsvNewFromInteger(0);
      else if (!jsvIsBoolean(n)) result=jsvAsNumber(n);
      jsvUnLock(n);
    }
  } else if (jsvIsUndefined(val) || jsvIsObject(val))
    result = 0;
  else {
    if (jsvIsString(val) && jsvIsEmptyString(val)) {
      result = jsvNewFromInteger(0);
    } else
      result = jsvAsNumber(val);
  }
  jsvUnLock(val);
  if (result) return result;
  return jsvNewFromFloat(NAN);
}
JsVar *jswrap_number_toFixed(JsVar *parent, int decimals) {
  if (decimals<0) decimals=0;
  if (decimals>20) decimals=20;
  char buf[70];
  ftoa_bounded_extra(jsvGetFloat(parent), buf, sizeof(buf), 10, decimals);
  return jsvNewFromString(buf);
}
JsVarInt jswrap_stream_available(JsVar *parent);
JsVar *jswrap_stream_read(JsVar *parent, JsVarInt chars);
bool jswrap_stream_pushData(JsVar *parent, JsVar *dataString, bool force);
JsVar *jswrap_object_constructor(JsVar *value) {
  if (jsvIsObject(value) || jsvIsArray(value) || jsvIsFunction(value))
    return jsvLockAgain(value);
  const char *objName = jswGetBasicObjectName(value);
  JsVar *funcName = objName ? jspGetNamedVariable(objName) : 0;
  if (!funcName) return jsvNewObject();
  JsVar *func = jsvSkipName(funcName);
  JsVar *result = jspeFunctionCall(func, funcName, 0, false, 1, &value);
  jsvUnLock2(funcName, func);
  return result;
}
JsVar *jswrap_object_length(JsVar *parent) {
  JsVarInt l;
  if (jsvIsArray(parent)) {
    l = jsvGetArrayLength(parent);
  } else if (jsvIsArrayBuffer(parent)) {
    l = (JsVarInt)jsvGetArrayBufferLength(parent);
  } else if (jsvIsString(parent)) {
    l = (JsVarInt)jsvGetStringLength(parent);
  } else if (jsvIsFunction(parent)) {
    JsVar *args = jsvGetFunctionArgumentLength(parent);
    l = jsvGetArrayLength(args);
    jsvUnLock(args);
  } else
    return 0;
  return jsvNewFromInteger(l);
}
JsVar *jswrap_object_valueOf(JsVar *parent) {
  if (!parent) {
    jsExceptionHere(JSET_TYPEERROR, "Invalid type %t for valueOf", parent);
    return 0;
  }
  return jsvLockAgain(parent);
}
JsVar *jswrap_object_toString(JsVar *parent, JsVar *arg0) {
  if (jsvIsInt(arg0) && jsvIsNumeric(parent)) {
    JsVarInt radix = jsvGetInteger(arg0);
    if (radix>=2 && radix<=36) {
      char buf[70];
      if (jsvIsInt(parent))
        itostr(jsvGetInteger(parent), buf, (unsigned int)radix);
      else
        ftoa_bounded_extra(jsvGetFloat(parent), buf, sizeof(buf), (int)radix, -1);
      return jsvNewFromString(buf);
    }
  }
  return jsvAsString(parent);
}
JsVar *jswrap_object_clone(JsVar *parent) {
  if (!parent) return 0;
  return jsvCopy(parent, true);
}
static void _jswrap_object_keys_or_property_names_iterator(
    const JswSymList *symbols,
    void (*callback)(void *data, JsVar *name),
    void *data) {
  if (!symbols) return;
  unsigned int i;
  unsigned char symbolCount = (*(uint8_t*)(&symbols->symbolCount));
  for (i=0;i<symbolCount;i++) {
    unsigned short strOffset = ((&symbols->symbols[i])->strOffset);
    JsVar *name = jsvNewFromString(&symbols->symbolChars[strOffset]);
    callback(data, name);
    jsvUnLock(name);
  }
}
void jswrap_object_keys_or_property_names_cb(
    JsVar *obj,
    JswObjectKeysOrPropertiesFlags flags,
    void (*callback)(void *data, JsVar *name),
    void *data
) {
  if (jsvIsIterable(obj) && !(jsvIsArrayBuffer(obj) && (flags&JSWOKPF_NO_INCLUDE_ARRAYBUFFER))) {
    JsvIsInternalChecker checkerFunction = jsvGetInternalFunctionCheckerFor(obj);
    JsvIterator it;
    jsvIteratorNew(&it, obj, JSIF_DEFINED_ARRAY_ElEMENTS);
    while (jsvIteratorHasElement(&it)) {
      JsVar *key = jsvIteratorGetKey(&it);
      if (!(checkerFunction && checkerFunction(key)) || (jsvIsStringEqual(key, "constructor"))) {
        JsVar *name = jsvAsStringAndUnLock(jsvCopyNameOnly(key, false, false));
        if (name) {
          callback(data, name);
          jsvUnLock(name);
        }
      }
      jsvUnLock(key);
      jsvIteratorNext(&it);
    }
    jsvIteratorFree(&it);
  }
  if (flags & JSWOKPF_INCLUDE_NON_ENUMERABLE) {
    const JswSymList *objSymbols = jswGetSymbolListForObjectProto(0);
    JsVar *protoOwner = jspGetPrototypeOwner(obj);
    if (protoOwner) {
      const JswSymList *symbols = jswGetSymbolListForObjectProto(protoOwner);
      jsvUnLock(protoOwner);
      _jswrap_object_keys_or_property_names_iterator(symbols, callback, data);
    } else if (!jsvIsObject(obj) || jsvIsRoot(obj)) {
       const JswSymList *symbols = jswGetSymbolListForObject(obj);
      _jswrap_object_keys_or_property_names_iterator(symbols, callback, data);
    }
    if (flags & JSWOKPF_INCLUDE_PROTOTYPE) {
      JsVar *proto = 0;
      if (jsvIsObject(obj) || jsvIsFunction(obj)) {
        proto = jsvObjectGetChildIfExists(obj, "__proto__");
      }
      if (jsvIsObject(proto)) {
        jswrap_object_keys_or_property_names_cb(proto, flags, callback, data);
      } else {
        const JswSymList *symbols = jswGetSymbolListForObjectProto(obj);
        _jswrap_object_keys_or_property_names_iterator(symbols, callback, data);
        if (objSymbols!=symbols)
          _jswrap_object_keys_or_property_names_iterator(objSymbols, callback, data);
      }
      jsvUnLock(proto);
    }
    if (jsvIsArray(obj) || jsvIsString(obj)) {
      JsVar *name = jsvNewFromString("length");
      callback(data, name);
      jsvUnLock(name);
    }
  }
}
JsVar *jswrap_object_keys_or_property_names(
    JsVar *obj,
    JswObjectKeysOrPropertiesFlags flags
    ) {
  JsVar *arr = jsvNewEmptyArray();
  if (!arr) return 0;
  jswrap_object_keys_or_property_names_cb(obj, flags, (void (*)(void *, JsVar *))jsvArrayAddUnique, arr);
  return arr;
}
void _jswrap_object_values_cb(void *data, JsVar *name) {
  JsVar **cbData = (JsVar**)data;
  JsVar *field = jsvAsArrayIndex(name);
  jsvArrayPushAndUnLock(cbData[0], jspGetVarNamedField(cbData[1], field, false));
  jsvUnLock(field);
}
void _jswrap_object_entries_cb(void *data, JsVar *name) {
  JsVar **cbData = (JsVar**)data;
  JsVar *tuple = jsvNewEmptyArray();
  if (!tuple) return;
  jsvArrayPush(tuple, name);
  JsVar *field = jsvAsArrayIndex(name);
  jsvArrayPushAndUnLock(tuple, jspGetVarNamedField(cbData[1], field, false));
  jsvUnLock(field);
  jsvArrayPushAndUnLock(cbData[0], tuple);
}
JsVar *jswrap_object_values_or_entries(JsVar *object, bool returnEntries) {
  JsVar *cbData[2];
  cbData[0] = jsvNewEmptyArray();
  cbData[1] = object;
  if (!cbData[0]) return 0;
  jswrap_object_keys_or_property_names_cb(
      object, JSWOKPF_NONE,
      returnEntries ? _jswrap_object_entries_cb : _jswrap_object_values_cb,
      (void*)cbData
  );
  return cbData[0];
}
JsVar *jswrap_object_fromEntries(JsVar *entries) {
  if (!jsvIsArray(entries)) return 0;
  JsVar *obj = jsvNewObject();
  if (!obj) return 0;
  JsvObjectIterator it;
  jsvObjectIteratorNew(&it, entries);
  while (jsvObjectIteratorHasValue(&it)) {
    JsVar *e = jsvObjectIteratorGetValue(&it);
    if (jsvIsArray(e)) {
      JsVar *key = jsvGetArrayItem(e, 0);
      JsVar *value = jsvGetArrayItem(e, 1);
      if (jsvIsString(key))
        jsvObjectSetChildVar(obj, key, value);
      jsvUnLock2(key,value);
    }
    jsvUnLock(e);
    jsvObjectIteratorNext(&it);
  }
  return obj;
}
JsVar *jswrap_object_create(JsVar *proto, JsVar *propertiesObject) {
  if (!jsvIsObject(proto) && !jsvIsNull(proto)) {
    jsExceptionHere(JSET_TYPEERROR, "Object prototype may only be an Object or null: %t", proto);
    return 0;
  }
  if (jsvIsObject(propertiesObject)) {
    jsExceptionHere(JSET_ERROR, "propertiesObject is not supported yet");
  }
  JsVar *obj = jsvNewObject();
  if (!obj) return 0;
  if (jsvIsObject(proto))
    jsvObjectSetChild(obj, "__proto__", proto);
  return obj;
}
JsVar *jswrap_object_getOwnPropertyDescriptor(JsVar *parent, JsVar *name) {
  if (!jswrap_object_hasOwnProperty(parent, name))
    return 0;
  JsVar *propName = jsvAsArrayIndex(name);
  JsVar *varName = jspGetVarNamedField(parent, propName, true);
  jsvUnLock(propName);
  do { if (!(varName)) jsAssertFail("bin/espruino_embedded.c",20252,""); } while(0);
  if (!varName) return 0;
  JsVar *obj = jsvNewObject();
  if (!obj) {
    jsvUnLock(varName);
    return 0;
  }
  bool isBuiltIn = jsvIsNewChild(varName);
  JsvIsInternalChecker checkerFunction = jsvGetInternalFunctionCheckerFor(parent);
  jsvObjectSetChildAndUnLock(obj, "writable", jsvNewFromBool(!jsvIsConstant(varName)));
  jsvObjectSetChildAndUnLock(obj, "enumerable", jsvNewFromBool(!checkerFunction || !checkerFunction(varName)));
  jsvObjectSetChildAndUnLock(obj, "configurable", jsvNewFromBool(!isBuiltIn));
  JsVar *getset = jsvGetValueOfName(varName);
  if (jsvIsGetterOrSetter(getset)) {
    jsvObjectSetChildAndUnLock(obj, "get", jsvObjectGetChildIfExists(getset,"get"));
    jsvObjectSetChildAndUnLock(obj, "set", jsvObjectGetChildIfExists(getset,"set"));
  } else {
    jsvObjectSetChildAndUnLock(obj, "value", jsvSkipName(varName));
  }
  jsvUnLock(getset);
  jsvUnLock(varName);
  return obj;
}
JsVar *jswrap_object_getOwnPropertyDescriptors(JsVar *parent) {
  if (!jsvHasChildren(parent)) {
    jsExceptionHere(JSET_TYPEERROR, "First argument must be Object, got %t", parent);
    return 0;
  }
  JsVar *descriptors = jsvNewObject();
  if (!descriptors) return 0;
  JsVar *ownPropertyNames = jswrap_object_keys_or_property_names(parent, JSWOKPF_INCLUDE_NON_ENUMERABLE);
  JsvObjectIterator it;
  jsvObjectIteratorNew(&it, ownPropertyNames);
  while (jsvObjectIteratorHasValue(&it)) {
    JsVar *propName = jsvObjectIteratorGetValue(&it);
    JsVar *propValue = jswrap_object_getOwnPropertyDescriptor(parent, propName);
    jsvObjectSetChildVar(descriptors, propName, propValue);
    jsvUnLock2(propName, propValue);
    jsvObjectIteratorNext(&it);
  }
  jsvObjectIteratorFree(&it);
  jsvUnLock(ownPropertyNames);
  return descriptors;
}
bool jswrap_object_hasOwnProperty(JsVar *parent, JsVar *name) {
  JsVar *propName = jsvAsArrayIndex(name);
  bool contains = false;
  if (jsvHasChildren(parent)) {
    JsVar *foundVar = jsvFindChildFromVar(parent, propName, false);
    if (foundVar) {
      contains = true;
      jsvUnLock(foundVar);
    }
  }
  if (!contains && !jsvIsObject(parent)) {
    const JswSymList *symbols = jswGetSymbolListForObject(parent);
    if (symbols) {
      char str[32];
      jsvGetString(propName, str, sizeof(str));
      JsVar *v = jswBinarySearch(symbols, parent, str);
      if (v) contains = true;
      jsvUnLock(v);
    }
  }
  jsvUnLock(propName);
  return contains;
}
JsVar *jswrap_object_defineProperty(JsVar *parent, JsVar *propName, JsVar *desc) {
  if (!jsvIsObject(parent) && !jsvIsFunction(parent) && !jsvIsArray(parent)) {
    jsExceptionHere(JSET_ERROR, "First argument must be Object, Function or Array, got %t", parent);
    return 0;
  }
  if (!jsvIsObject(desc)) {
    jsExceptionHere(JSET_ERROR, "Property description must be Object, got %t", desc);
    return 0;
  }
  JsVar *name = jsvAsArrayIndex(propName);
  JsVar *value = 0;
  JsVar *getter = jsvObjectGetChildIfExists(desc, "get");
  JsVar *setter = jsvObjectGetChildIfExists(desc, "set");
  if (getter || setter) {
    value = jsvNewWithFlags(JSV_GET_SET);
    if (value) {
      if (getter) jsvObjectSetChild(value, "get", getter);
      if (setter) jsvObjectSetChild(value, "set", setter);
    }
    jsvUnLock2(getter,setter);
  }
  if (!value) value = jsvObjectGetChildIfExists(desc, "value");
  jsvObjectSetChildVar(parent, name, value);
  JsVar *writable = jsvObjectGetChildIfExists(desc, "writable");
  if (!jsvIsUndefined(writable) && !jsvGetBoolAndUnLock(writable))
    name->flags |= JSV_CONSTANT;
  jsvUnLock2(name, value);
  return jsvLockAgain(parent);
}
JsVar *jswrap_object_defineProperties(JsVar *parent, JsVar *props) {
  if (!jsvIsObject(parent)) {
    jsExceptionHere(JSET_ERROR, "First argument must be Object, got %t", parent);
    return 0;
  }
  if (!jsvIsObject(props)) {
    jsExceptionHere(JSET_ERROR, "Second argument must be Object, got %t", props);
    return 0;
  }
  JsvObjectIterator it;
  jsvObjectIteratorNew(&it, props);
  while (jsvObjectIteratorHasValue(&it)) {
    JsVar *name = jsvObjectIteratorGetKey(&it);
    JsVar *desc = jsvObjectIteratorGetValue(&it);
    jsvUnLock3(jswrap_object_defineProperty(parent, name, desc), name, desc);
    jsvObjectIteratorNext(&it);
  }
  jsvObjectIteratorFree(&it);
  return jsvLockAgain(parent);
}
JsVar *jswrap_object_getPrototypeOf(JsVar *object) {
  return jspGetNamedField(object, "__proto__", false);
}
JsVar *jswrap_object_setPrototypeOf(JsVar *object, JsVar *proto) {
  JsVar *v = (jsvIsFunction(object)||jsvIsObject(object)) ? jsvFindOrAddChildFromString(object, "__proto__") : 0;
  if (!jsvIsName(v)) {
    jsExceptionHere(JSET_TYPEERROR, "Can't extend %t", v);
  } else {
    jsvSetValueOfName(v, proto);
  }
  jsvUnLock(v);
  return jsvLockAgainSafe(object);
}
JsVar *jswrap_object_assign(JsVar *args) {
  JsVar *result = 0;
  JsvObjectIterator argsIt;
  jsvObjectIteratorNew(&argsIt, args);
  bool error = false;
  while (!error && jsvObjectIteratorHasValue(&argsIt)) {
    JsVar *arg = jsvObjectIteratorGetValue(&argsIt);
    if (jsvIsUndefined(arg) || jsvIsNull(arg)) {
    } else if (!jsvIsObject(arg)) {
      jsExceptionHere(JSET_TYPEERROR, "Expecting Object, got %t", arg);
      error = true;
    } else if (!result) {
      result = jsvLockAgain(arg);
    } else {
      jsvObjectAppendAll(result, arg);
    }
    jsvUnLock(arg);
    jsvObjectIteratorNext(&argsIt);
  };
  jsvObjectIteratorFree(&argsIt);
  return result;
}
bool jswrap_boolean_constructor(JsVar *value) {
  return jsvGetBool(value);
}
void jswrap_object_removeListener(JsVar *parent, JsVar *event, JsVar *callback) {
  if (!jsvHasChildren(parent)) {
    jsExceptionHere(JSET_TYPEERROR, "Parent must be an object - not a String, Integer, etc");
    return;
  }
  if (jsvIsString(event)) {
    JsVar *eventName = jsvVarPrintf("#on""%v", event);
    if (!eventName) return;
    JsVar *eventListName = jsvFindChildFromVar(parent, eventName, true);
    jsvUnLock(eventName);
    JsVar *eventList = jsvSkipName(eventListName);
    if (eventList) {
      if (jsvIsArray(eventList)) {
        JsVar *idx = jsvGetIndexOf(eventList, callback, true);
        if (idx)
          jsvRemoveChildAndUnLock(eventList, idx);
      }
      jsvUnLock(eventList);
    }
    jsvUnLock(eventListName);
  } else {
    jsExceptionHere(JSET_TYPEERROR, "First argument must be String");
    return;
  }
}
void jswrap_object_removeAllListeners(JsVar *parent, JsVar *event) {
  if (!jsvHasChildren(parent)) {
    jsExceptionHere(JSET_TYPEERROR, "Parent must be an object - not a String, Integer, etc");
    return;
  }
  if (jsvIsString(event)) {
    JsVar *eventName = jsvVarPrintf("#on""%v", event);
    if (!eventName) return;
    JsVar *eventList = jsvFindChildFromVar(parent, eventName, true);
    jsvUnLock(eventName);
    if (eventList) {
      jsvRemoveChildAndUnLock(parent, eventList);
    }
  } else if (jsvIsUndefined(event)) {
    JsvObjectIterator it;
    jsvObjectIteratorNew(&it, parent);
    while (jsvObjectIteratorHasValue(&it)) {
      JsVar *key = jsvObjectIteratorGetKey(&it);
      jsvObjectIteratorNext(&it);
      if (jsvIsStringEqualOrStartsWith(key, "#on", true)) {
        jsvRemoveChild(parent, key);
      }
      jsvUnLock(key);
    }
    jsvObjectIteratorFree(&it);
  } else {
    jsExceptionHere(JSET_TYPEERROR, "First argument must be String or undefined");
    return;
  }
}
void jswrap_object_removeAllListeners_cstr(JsVar *parent, const char *event) {
  JsVar *s = jsvNewFromString(event);
  if (s) {
    jswrap_object_removeAllListeners(parent, s);
    jsvUnLock(s);
  }
}
void jswrap_function_replaceWith(JsVar *oldFunc, JsVar *newFunc) {
  if ((!jsvIsFunction(oldFunc)) || (!jsvIsFunction(newFunc))) {
    jsExceptionHere(JSET_TYPEERROR, "Argument should be a function");
    return;
  }
  if (jsvIsNativeFunction(oldFunc) != jsvIsNativeFunction(newFunc)) {
    if (jsvIsNativeFunction(newFunc)) {
      oldFunc->flags = (oldFunc->flags&~JSV_VARTYPEMASK) | JSV_NATIVE_FUNCTION;
      oldFunc->varData.native = newFunc->varData.native;
    } else {
      memset(&oldFunc->varData.native, 0, sizeof(oldFunc->varData.native));
      oldFunc->flags = (oldFunc->flags&~JSV_VARTYPEMASK) | JSV_FUNCTION;
    }
  }
  if (jsvIsFunctionReturn(oldFunc) != jsvIsFunctionReturn(newFunc)) {
    if (jsvIsFunctionReturn(newFunc))
      oldFunc->flags = (oldFunc->flags&~JSV_VARTYPEMASK) | JSV_FUNCTION_RETURN;
    else
      oldFunc->flags = (oldFunc->flags&~JSV_VARTYPEMASK) | JSV_FUNCTION;
  }
  JsVar *scope = jsvFindChildFromString(oldFunc, "\xFF""sco");
  JsVar *prototype = jsvFindChildFromString(oldFunc, "prototype");
  jsvRemoveAllChildren(oldFunc);
  JsvObjectIterator it;
  jsvObjectIteratorNew(&it, newFunc);
  while (jsvObjectIteratorHasValue(&it)) {
    JsVar *el = jsvObjectIteratorGetKey(&it);
    jsvObjectIteratorNext(&it);
    if (!jsvIsStringEqual(el, "\xFF""sco") &&
        !jsvIsStringEqual(el, "prototype")) {
      JsVar *copy;
      if (jsvIsStringEqual(el, "\xFF""cod")
          ) {
        JsVar *fnCode = jsvSkipName(el);
        copy = jsvMakeIntoVariableName(jsvNewFromStringVarComplete(el), fnCode);
        jsvUnLock(fnCode);
      } else
        copy = jsvCopy(el, true);
      if (copy) {
        jsvAddName(oldFunc, copy);
        jsvUnLock(copy);
      }
    }
    jsvUnLock(el);
  }
  jsvObjectIteratorFree(&it);
  if (scope) jsvAddName(oldFunc, scope);
  jsvUnLock(scope);
  if (prototype) jsvAddName(oldFunc, prototype);
  jsvUnLock(prototype);
}
JsVar *jswrap_function_apply_or_call(JsVar *parent, JsVar *thisArg, JsVar *argsArray) {
  unsigned int i;
  JsVar **args = 0;
  unsigned int argC = 0;
  if (jsvIsIterable(argsArray)) {
    argC = (unsigned int)jsvGetLength(argsArray);
    if (argC>256) {
      jsExceptionHere(JSET_ERROR, "Array passed to Function.apply is too big! Maximum ""256"" arguments, got %d", argC);
      return 0;
    }
    args = (JsVar**)__builtin_alloca((size_t)argC * sizeof(JsVar*));
    for (i=0;i<argC;i++) args[i] = 0;
    JsvIterator it;
    jsvIteratorNew(&it, argsArray, JSIF_EVERY_ARRAY_ELEMENT);
    while (jsvIteratorHasElement(&it)) {
      JsVar *idxVar = jsvIteratorGetKey(&it);
      if (jsvIsIntegerish(idxVar)) {
        JsVarInt idx = jsvGetInteger(idxVar);
        if (idx>=0 && idx<(int)argC) {
          do { if (!(!args[idx])) jsAssertFail("bin/espruino_embedded.c",21043,""); } while(0);
          args[idx] = jsvIteratorGetValue(&it);
        }
      }
      jsvUnLock(idxVar);
      jsvIteratorNext(&it);
    }
    jsvIteratorFree(&it);
  } else if (!jsvIsUndefined(argsArray)) {
    jsExceptionHere(JSET_ERROR, "Second argument to Function.apply must be iterable, got %t", argsArray);
    return 0;
  }
  JsVar *r = jspeFunctionCall(parent, 0, thisArg, false, (int)argC, args);
  jsvUnLockMany(argC, args);
  return r;
}
JsVar *jswrap_function_bind(JsVar *parent, JsVar *thisArg, JsVar *argsArray) {
  if (!jsvIsFunction(parent)) {
    jsExceptionHere(JSET_TYPEERROR, "Function.bind expects to be called on function, got %t", parent);
    return 0;
  }
  JsVar *fn;
  if (jsvIsNativeFunction(parent))
    fn = jsvNewNativeFunction(parent->varData.native.ptr, parent->varData.native.argTypes);
  else
    fn = jsvNewWithFlags(jsvIsFunctionReturn(parent) ? JSV_FUNCTION_RETURN : JSV_FUNCTION);
  if (!fn) return 0;
  JsvObjectIterator fnIt;
  jsvObjectIteratorNew(&fnIt, parent);
  while (jsvObjectIteratorHasValue(&fnIt)) {
    JsVar *param = jsvObjectIteratorGetKey(&fnIt);
    JsVar *defaultValue = jsvObjectIteratorGetValue(&fnIt);
    bool wasBound = jsvIsFunctionParameter(param) && defaultValue;
    if (wasBound) {
      JsVar *newParam = jsvCopy(param, true);
      if (newParam) {
        jsvAddName(fn, newParam);
        jsvUnLock(newParam);
      }
    }
    jsvUnLock2(param, defaultValue);
    if (!wasBound) break;
    jsvObjectIteratorNext(&fnIt);
  }
  if (argsArray) {
    JsvObjectIterator argIt;
    jsvObjectIteratorNew(&argIt, argsArray);
    while (jsvObjectIteratorHasValue(&argIt)) {
      JsVar *defaultValue = jsvObjectIteratorGetValue(&argIt);
      bool addedParam = false;
      while (!addedParam && jsvObjectIteratorHasValue(&fnIt)) {
        JsVar *param = jsvObjectIteratorGetKey(&fnIt);
        if (!jsvIsFunctionParameter(param)) {
          jsvUnLock(param);
          break;
        }
        JsVar *newParam = jsvCopyNameOnly(param, false, true);
        jsvSetValueOfName(newParam, defaultValue);
        jsvAddName(fn, newParam);
        addedParam = true;
        jsvUnLock2(param, newParam);
        jsvObjectIteratorNext(&fnIt);
      }
      if (!addedParam) {
        jsvAddFunctionParameter(fn, 0, defaultValue);
      }
      jsvUnLock(defaultValue);
      jsvObjectIteratorNext(&argIt);
    }
    jsvObjectIteratorFree(&argIt);
  }
  while (jsvObjectIteratorHasValue(&fnIt)) {
    JsVar *param = jsvObjectIteratorGetKey(&fnIt);
    JsVar *newParam = jsvCopyNameOnly(param, true, true);
    if (newParam) {
      jsvAddName(fn, newParam);
      jsvUnLock(newParam);
    }
    jsvUnLock(param);
    jsvObjectIteratorNext(&fnIt);
  }
  jsvObjectIteratorFree(&fnIt);
  jsvObjectSetChild(fn, "\xFF""ths", thisArg);
  return fn;
}
typedef struct {
  JsVar *sourceStr;
  size_t startIndex;
  bool ignoreCase;
  bool rangeMatch;
  short rangeFirstChar;
  int groups;
  size_t groupStart[9];
  size_t groupEnd[9];
} matchInfo;
static JsVar *matchhere(char *regexp, JsvStringIterator *txtIt, matchInfo info);
static JsVar *matchfound(JsvStringIterator *txtIt, matchInfo info) {
  JsVar *rmatch = jsvNewEmptyArray();
  size_t endIndex = jsvStringIteratorGetIndex(txtIt);
  JsVar *matchStr = jsvNewFromStringVar(info.sourceStr, info.startIndex, endIndex-info.startIndex);
  jsvSetArrayItem(rmatch, 0, matchStr);
  jsvUnLock(matchStr);
  int i;
  for (i=0;i<info.groups;i++) {
    matchStr = jsvNewFromStringVar(info.sourceStr, info.groupStart[i], info.groupEnd[i]-info.groupStart[i]);
    jsvSetArrayItem(rmatch, i+1, matchStr);
    jsvUnLock(matchStr);
  }
  jsvObjectSetChildAndUnLock(rmatch, "index", jsvNewFromInteger((JsVarInt)info.startIndex));
  jsvObjectSetChild(rmatch, "input", info.sourceStr);
  return rmatch;
}
static JsVar *nomatchfound(char *regexp, matchInfo info) {
  if (!jspCheckStackPosition()) return 0;
  while (*regexp && *regexp!='|') {
    if (*regexp=='\\') {
      regexp++;
      if (!*regexp) return 0;
    }
    regexp++;
  }
  if (*regexp != '|') return 0;
  regexp++;
  JsvStringIterator txtIt;
  jsvStringIteratorNew(&txtIt, info.sourceStr, info.startIndex);
  JsVar *rmatch = matchhere(regexp, &txtIt, info);
  jsvStringIteratorFree(&txtIt);
  return rmatch;
}
static JsVar *match(char *regexp, JsVar *str, size_t startIndex, bool ignoreCase) {
  matchInfo info;
  info.sourceStr = str;
  info.startIndex = startIndex;
  info.ignoreCase = ignoreCase;
  info.rangeMatch = false;
  info.rangeFirstChar = 256;
  info.groups = 0;
  JsVar *rmatch;
  JsvStringIterator txtIt, txtIt2;
  jsvStringIteratorNew(&txtIt, str, startIndex);
  jsvStringIteratorClone(&txtIt2, &txtIt);
  rmatch = matchhere(regexp, &txtIt2, info);
  jsvStringIteratorFree(&txtIt2);
  jsvStringIteratorNext(&txtIt);
  while (!rmatch && jsvStringIteratorHasChar(&txtIt)) {
    info.startIndex++;
    jsvStringIteratorClone(&txtIt2, &txtIt);
    rmatch = matchhere(regexp, &txtIt2, info);
    jsvStringIteratorFree(&txtIt2);
    jsvStringIteratorNext(&txtIt);
  }
  jsvStringIteratorFree(&txtIt);
  return rmatch;
}
bool matchcharacter(char *regexp, JsvStringIterator *txtIt, int *length, matchInfo *info) {
  *length = 1;
  char ch = jsvStringIteratorGetChar(txtIt);
  if (regexp[0]=='.') return true;
  if (regexp[0]=='[') {
    info->rangeMatch = true;
    bool inverted = regexp[1]=='^';
    if (inverted) (*length)++;
    bool matchAny = false;
    while (regexp[*length] && regexp[*length]!=']') {
      int matchLen;
      if (regexp[*length]=='.') {
        matchAny |= ch=='.';
        (*length)++;
      } else {
        matchAny |= matchcharacter(&regexp[*length], txtIt, &matchLen, info);
        (*length) += matchLen;
      }
    }
    if (regexp[*length]==']') {
      (*length)++;
    } else {
      jsExceptionHere(JSET_ERROR, "Unfinished character set in RegEx");
      return false;
    }
    info->rangeMatch = false;
    return matchAny != inverted;
  }
  char cH = regexp[0];
  if (cH=='\\') {
    *length = 2;
    cH = regexp[1];
    if (cH=='d') return isNumeric(ch);
    if (cH=='D') return !isNumeric(ch);
    if (cH=='f') { cH=0x0C; goto haveCode; }
    if (cH=='b') { cH=0x08; goto haveCode; }
    if (cH=='n') { cH=0x0A; goto haveCode; }
    if (cH=='r') { cH=0x0D; goto haveCode; }
    if (cH=='s') return isWhitespace(ch);
    if (cH=='S') return !isWhitespace(ch);
    if (cH=='t') { cH=0x09; goto haveCode; }
    if (cH=='v') { cH=0x0B; goto haveCode; }
    if (cH=='w') return isNumeric(ch) || isAlpha(ch) || ch=='_';
    if (cH=='W') return !(isNumeric(ch) || isAlpha(ch) || ch=='_');
    if (cH=='0') { cH=0; goto haveCode; }
    if (cH>='1' && cH<='9') {
      jsExceptionHere(JSET_ERROR, "Backreferences not supported");
      return false;
    }
    if (cH=='x' && regexp[2] && regexp[3]) {
      *length = 4;
      cH = (char)hexToByte(regexp[2],regexp[3]);
      goto haveCode;
    }
  }
haveCode:
  if (info->rangeMatch && regexp[*length] == '-' && regexp[1+*length] != ']') {
    info->rangeFirstChar = cH;
    (*length)++;
    int matchLen;
    bool match = matchcharacter(&regexp[*length], txtIt, &matchLen, info);
    (*length)+=matchLen;
    return match;
  }
  if (info->ignoreCase) {
    ch = charToLowerCase(ch);
    cH = charToLowerCase(cH);
  }
  if (info->rangeFirstChar != 256) {
    char cL = (char)info->rangeFirstChar;
    if (info->ignoreCase) {
      cL = charToLowerCase(cL);
    }
    info->rangeFirstChar = 256;
    return (ch >= cL && ch <= cH && cL < cH);
  }
  return cH==ch;
}
static JsVar *matchhere(char *regexp, JsvStringIterator *txtIt, matchInfo info) {
  if (jspIsInterrupted()) return 0;
  if (regexp[0] == '\0' ||
      regexp[0] == '|')
    return matchfound(txtIt, info);
  if (regexp[0] == '^') {
    if (jsvStringIteratorGetIndex(txtIt)!=0)
      return 0;
    if (!jspCheckStackPosition()) return 0;
    return matchhere(regexp+1, txtIt, info);
  }
  if (regexp[0] == '$') {
    if (!jsvStringIteratorHasChar(txtIt))
      return matchhere(regexp+1, txtIt, info);
    else
      return nomatchfound(regexp+1, info);
  }
  if (regexp[0] == '(') {
    info.groupStart[info.groups] = jsvStringIteratorGetIndex(txtIt);
    info.groupEnd[info.groups] = info.groupStart[info.groups];
    if (info.groups<9) info.groups++;
    if (!jspCheckStackPosition()) return 0;
    return matchhere(regexp+1, txtIt, info);
  }
  if (regexp[0] == ')') {
    if (info.groups>0)
      info.groupEnd[info.groups-1] = jsvStringIteratorGetIndex(txtIt);
    if (!jspCheckStackPosition()) return 0;
    return matchhere(regexp+1, txtIt, info);
  }
  int charLength;
  bool charMatched = matchcharacter(regexp, txtIt, &charLength, &info);
  if (regexp[charLength] == '*' || regexp[charLength] == '+') {
    char op = regexp[charLength];
    if (!charMatched && op=='+') {
      return nomatchfound(&regexp[charLength+1], info);
    }
    char *regexpAfterStar = regexp+charLength+1;
    JsvStringIterator txtIt2;
    jsvStringIteratorClone(&txtIt2, txtIt);
    JsVar *lastrmatch = matchhere(regexpAfterStar, &txtIt2, info);
    jsvStringIteratorFree(&txtIt2);
    while (jsvStringIteratorHasChar(txtIt) && charMatched) {
      jsvStringIteratorNext(txtIt);
      charMatched = matchcharacter(regexp, txtIt, &charLength, &info);
      jsvStringIteratorClone(&txtIt2, txtIt);
      JsVar *rmatch = matchhere(regexpAfterStar, &txtIt2, info);
      jsvStringIteratorFree(&txtIt2);
      if (rmatch) {
        jsvUnLock(lastrmatch);
        lastrmatch = rmatch;
      }
    }
    return lastrmatch;
  }
  if (jsvStringIteratorHasChar(txtIt) && charMatched) {
    jsvStringIteratorNext(txtIt);
    if (!jspCheckStackPosition()) return 0;
    return matchhere(regexp+charLength, txtIt, info);
  }
  return nomatchfound(&regexp[charLength], info);
}
JsVar *jswrap_regexp_constructor(JsVar *str, JsVar *flags) {
  if (!jsvIsString(str)) {
    jsExceptionHere(JSET_TYPEERROR, "Expecting String as first argument, got %t", str);
    return 0;
  }
  JsVar *r = jspNewObject(0,"RegExp");
  jsvObjectSetChild(r, "source", str);
  if (!jsvIsUndefined(flags)) {
    if (!jsvIsString(flags))
      jsExceptionHere(JSET_TYPEERROR, "Expecting String as first argument, got %t", str);
    else
      jsvObjectSetChild(r, "flags", flags);
  }
  jsvObjectSetChildAndUnLock(r, "lastIndex", jsvNewFromInteger(0));
  JsvStringIterator it;
  jsvStringIteratorNew(&it,str,0);
  char ch = 0;
  bool noControlChars = true;
  char buf[32];
  size_t bufc = 0;
  while (jsvStringIteratorHasChar(&it)) {
    bool wasSlash = ch=='\\';
    if (ch && strchr(".[]()|^*+$",ch)) noControlChars = false;
    ch = jsvStringIteratorGetCharAndNext(&it);
    if (wasSlash) {
      if (!strchr(".\\",ch)) {
        noControlChars = false;
        ch = 0;
      }
    }
    if (ch && (wasSlash || ch!='\\') && bufc<sizeof(buf)) buf[bufc++]=ch;
    if (wasSlash) ch = 0;
  }
  jsvStringIteratorFree(&it);
  if (ch=='$' && noControlChars && bufc<sizeof(buf)) {
    bufc--;
    jsvObjectSetChildAndUnLock(r, "endsWith", jsvNewStringOfLength((unsigned int)bufc,buf));
  }
  return r;
}
JsVar *jswrap_regexp_exec(JsVar *parent, JsVar *arg) {
  JsVar *str = jsvAsString(arg);
  JsVarInt lastIndex = jsvObjectGetIntegerChild(parent, "lastIndex");
  JsVar *endsWith = jsvObjectGetChildIfExists(parent, "endsWith");
  if (endsWith) {
    int idx = (int)jsvGetStringLength(arg) - (int)jsvGetStringLength(endsWith);
    if ((lastIndex <= idx) && jsvCompareString(arg, endsWith, (size_t)idx,0,true)==0) {
      JsVar *rmatch = jsvNewEmptyArray();
      jsvSetArrayItem(rmatch, 0, endsWith);
      jsvObjectSetChildAndUnLock(rmatch, "index", jsvNewFromInteger(idx));
      jsvObjectSetChildAndUnLock(rmatch, "input", str);
      jsvUnLock(endsWith);
      return rmatch;
    }
    jsvUnLock(endsWith);
  }
  JsVar *regex = jsvObjectGetChildIfExists(parent, "source");
  if (!jsvIsString(regex) || lastIndex>(JsVarInt)jsvGetStringLength(str)) {
    jsvUnLock2(str,regex);
    return 0;
  }
  size_t regexLen = jsvGetStringLength(regex);
  char *regexPtr = (char *)__builtin_alloca(regexLen+1);
  if (!regexPtr) {
    jsvUnLock2(str,regex);
    return 0;
  }
  jsvGetString(regex, regexPtr, regexLen+1);
  jsvUnLock(regex);
  JsVar *rmatch = match(regexPtr, str, (size_t)lastIndex, jswrap_regexp_hasFlag(parent,'i'));
  jsvUnLock(str);
  if (!rmatch) {
    rmatch = jsvNewWithFlags(JSV_NULL);
    lastIndex = 0;
  } else {
    if (jswrap_regexp_hasFlag(parent,'g')) {
      JsVar *matchStr = jsvGetArrayItem(rmatch,0);
      lastIndex = jsvObjectGetIntegerChild(rmatch, "index") +
                  (JsVarInt)jsvGetStringLength(matchStr);
      jsvUnLock(matchStr);
    } else
      lastIndex = 0;
  }
  jsvObjectSetChildAndUnLock(parent, "lastIndex", jsvNewFromInteger(lastIndex));
  return rmatch;
}
bool jswrap_regexp_test(JsVar *parent, JsVar *str) {
  JsVar *v = jswrap_regexp_exec(parent, str);
  bool r = v && !jsvIsNull(v);
  jsvUnLock(v);
  return r;
}
bool jswrap_regexp_hasFlag(JsVar *parent, char flag) {
  JsVar *flags = jsvObjectGetChildIfExists(parent, "flags");
  bool has = false;
  if (jsvIsString(flags)) {
    JsvStringIterator it;
    jsvStringIteratorNew(&it, flags, 0);
    while (jsvStringIteratorHasChar(&it)) {
      has |= jsvStringIteratorGetCharAndNext(&it)==flag;
    }
    jsvStringIteratorFree(&it);
  }
  jsvUnLock(flags);
  return has;
}
JsVar *jswrap_string_constructor(JsVar *args) {
  if (jsvGetArrayLength(args)==0)
    return jsvNewFromEmptyString();
  return jsvAsStringAndUnLock(jsvGetArrayItem(args, 0));
}
JsVar *jswrap_string_fromCharCode(JsVar *arr) {
  do { if (!(jsvIsArray(arr))) jsAssertFail("bin/espruino_embedded.c",21707,""); } while(0);
  JsVar *r = jsvNewFromEmptyString();
  if (!r) return 0;
  JsvObjectIterator it;
  jsvObjectIteratorNew(&it, arr);
  while (jsvObjectIteratorHasValue(&it)) {
    char ch = (char)jsvGetIntegerAndUnLock(jsvObjectIteratorGetValue(&it));
    jsvAppendStringBuf(r, &ch, 1);
    jsvObjectIteratorNext(&it);
  }
  jsvObjectIteratorFree(&it);
  return r;
}
int _jswrap_string_charCodeAt(JsVar *parent, JsVarInt idx) {
  if (!jsvIsString(parent)) return -1;
  JsvStringIterator it;
  jsvStringIteratorNewUTF8(&it, parent, (size_t)idx);
  int uChar = jsvStringIteratorGetUTF8CharAndNext(&it);
  jsvStringIteratorFree(&it);
  return uChar;
}
JsVar *jswrap_string_charAt_undefined(JsVar *parent, JsVarInt idx) {
  int uChar = _jswrap_string_charCodeAt(parent, idx);
  if (uChar<0) return 0;
  {
    char ch = (char)uChar;
    return jsvNewStringOfLength(1, &ch);
  }
}
JsVar *jswrap_string_charAt(JsVar *parent, JsVarInt idx) {
  JsVar *v = jswrap_string_charAt_undefined(parent, idx);
  if (v) return v;
  return jsvNewFromEmptyString();
}
JsVar *jswrap_string_charCodeAt(JsVar *parent, JsVarInt idx) {
  int uChar = _jswrap_string_charCodeAt(parent, idx);
  if (uChar<0) return jsvNewFromFloat(NAN);
  return jsvNewFromInteger(uChar);
}
int jswrap_string_indexOf(JsVar *parent, JsVar *substring, JsVar *fromIndex, bool lastIndexOf) {
  if (!jsvIsString(parent)) return 0;
  substring = jsvAsString(substring);
  if (!substring) return 0;
  int parentLength = (int)jsvGetStringLength(parent);
  int subStringLength = (int)jsvGetStringLength(substring);
  if (subStringLength > parentLength) {
    jsvUnLock(substring);
    return -1;
  }
  int lastPossibleSearch = parentLength - subStringLength;
  int idx, dir, end;
  if (!lastIndexOf) {
    dir = 1;
    end = lastPossibleSearch+1;
    idx = 0;
    if (jsvIsNumeric(fromIndex)) {
      idx = (int)jsvGetInteger(fromIndex);
      if (idx<0) idx=0;
      if (idx>end) idx=end;
    }
  } else {
    dir = -1;
    end = -1;
    idx = lastPossibleSearch;
    if (jsvIsNumeric(fromIndex)) {
      idx = (int)jsvGetInteger(fromIndex);
      if (idx<0) idx=0;
      if (idx>lastPossibleSearch) idx=lastPossibleSearch;
    }
  }
  for (;idx!=end;idx+=dir) {
    if (jsvCompareString(parent, substring, (size_t)idx, 0, true)==0) {
      jsvUnLock(substring);
      return idx;
    }
  }
  jsvUnLock(substring);
  return -1;
}
JsVar *jswrap_string_match(JsVar *parent, JsVar *subStr) {
  if (!jsvIsString(parent)) return 0;
  if (jsvIsUndefined(subStr)) return 0;
  if (jsvIsInstanceOf(subStr, "RegExp")) {
    jsvObjectSetChildAndUnLock(subStr, "lastIndex", jsvNewFromInteger(0));
    JsVar *match;
    match = jswrap_regexp_exec(subStr, parent);
    if (!jswrap_regexp_hasFlag(subStr,'g')) {
      return match;
    }
    JsVar *array = jsvNewEmptyArray();
    if (!array) return 0;
    while (match && !jsvIsNull(match)) {
      JsVar *matchStr = jsvGetArrayItem(match,0);
      JsVarInt idx = jsvObjectGetIntegerChild(match,"index");
      JsVarInt len = (JsVarInt)jsvGetStringLength(matchStr);
      int last = idx+len;
      jsvArrayPushAndUnLock(array, matchStr);
      jsvUnLock(match);
      jsvObjectSetChildAndUnLock(subStr, "lastIndex", jsvNewFromInteger(last + (len?0:1)));
      match = jswrap_regexp_exec(subStr, parent);
    }
    jsvUnLock(match);
    jsvObjectSetChildAndUnLock(subStr, "lastIndex", jsvNewFromInteger(0));
    return array;
  }
  subStr = jsvAsString(subStr);
  int idx = jswrap_string_indexOf(parent, subStr, 0, false);
  if (idx>=0) {
      JsVar *array = jsvNewEmptyArray();
      if (!array) {
        jsvUnLock(subStr);
        return 0;
      }
      jsvArrayPush(array, subStr);
      jsvObjectSetChildAndUnLock(array, "index", jsvNewFromInteger(idx));
      jsvObjectSetChildAndUnLock(array, "input", subStr);
      return array;
  }
  jsvUnLock(subStr);
  return jsvNewNull();
}
static JsVar *_jswrap_string_replace(JsVar *parent, JsVar *subStr, JsVar *newSubStr, bool replaceAll) {
  JsVar *str = jsvAsString(parent);
  if (jsvIsInstanceOf(subStr, "RegExp")) {
    JsVar *replace;
    if (jsvIsFunction(newSubStr) || jsvIsString(newSubStr))
      replace = jsvLockAgain(newSubStr);
    else
      replace = jsvAsString(newSubStr);
    jsvObjectSetChildAndUnLock(subStr, "lastIndex", jsvNewFromInteger(0));
    bool global = jswrap_regexp_hasFlag(subStr,'g');
    if (replaceAll) global = true;
    JsVar *newStr = jsvNewFromEmptyString();
    JsvStringIterator dst;
    jsvStringIteratorNew(&dst, newStr, 0);
    JsVarInt lastIndex = 0;
    JsVar *match;
    match = jswrap_regexp_exec(subStr, str);
    while (match && !jsvIsNull(match) && !jspIsInterrupted()) {
      JsVar *matchStr = jsvGetArrayItem(match,0);
      JsVarInt idx = jsvObjectGetIntegerChild(match,"index");
      JsVarInt len = (JsVarInt)jsvGetStringLength(matchStr);
      jsvStringIteratorAppendString(&dst, str, (size_t)lastIndex, (idx-lastIndex));
      if (jsvIsFunction(replace)) {
        unsigned int argCount = 0;
        JsVar *args[13];
        args[argCount++] = jsvLockAgain(matchStr);
        JsVar *v;
        while ((v = jsvGetArrayItem(match, (JsVarInt)argCount)) && argCount<11)
          args[argCount++] = v;
        args[argCount++] = jsvObjectGetChildIfExists(match,"index");
        args[argCount++] = jsvObjectGetChildIfExists(match,"input");
        JsVar *result = jsvAsStringAndUnLock(jspeFunctionCall(replace, 0, 0, false, (JsVarInt)argCount, args));
        jsvUnLockMany(argCount, args);
        jsvStringIteratorAppendString(&dst, result, 0, (0x7FFFFFFF));
        jsvUnLock(result);
      } else {
        JsvStringIterator src;
        jsvStringIteratorNew(&src, replace, 0);
        while (jsvStringIteratorHasChar(&src)) {
          char ch = jsvStringIteratorGetCharAndNext(&src);
          if (ch=='$') {
            ch = jsvStringIteratorGetCharAndNext(&src);
            JsVar *group = 0;
            if (ch>'0' && ch<='9')
              group = jsvGetArrayItem(match, ch-'0');
            if (group) {
              jsvStringIteratorAppendString(&dst, group, 0, (0x7FFFFFFF));
              jsvUnLock(group);
            } else {
              jsvStringIteratorAppend(&dst, '$');
              jsvStringIteratorAppend(&dst, ch);
            }
          } else {
            jsvStringIteratorAppend(&dst, ch);
          }
        }
        jsvStringIteratorFree(&src);
      }
      lastIndex = idx+len;
      jsvUnLock(matchStr);
      jsvUnLock(match);
      match = 0;
      if (global) {
        jsvObjectSetChildAndUnLock(subStr, "lastIndex", jsvNewFromInteger(lastIndex + (len?0:1)));
        match = jswrap_regexp_exec(subStr, str);
      }
    }
    jsvStringIteratorAppendString(&dst, str, (size_t)lastIndex, (0x7FFFFFFF));
    jsvStringIteratorFree(&dst);
    jsvUnLock3(match,replace,str);
    if (global)
      jsvObjectSetChildAndUnLock(subStr, "lastIndex", jsvNewFromInteger(0));
    return newStr;
  }
  newSubStr = jsvAsString(newSubStr);
  subStr = jsvAsString(subStr);
  int idx = jswrap_string_indexOf(str, subStr, NULL, false);
  while (idx>=0 && !jspIsInterrupted()) {
    JsVar *newStr = jsvNewWritableStringFromStringVar(str, 0, (size_t)idx);
    jsvAppendStringVarComplete(newStr, newSubStr);
    jsvAppendStringVar(newStr, str, (size_t)idx+jsvGetStringLength(subStr), (0x7FFFFFFF));
    jsvUnLock(str);
    str = newStr;
    if (replaceAll) {
      JsVar *fromIdx = jsvNewFromInteger(idx + (int)jsvGetStringLength(newSubStr));
      idx = jswrap_string_indexOf(str, subStr, fromIdx, false);
      jsvUnLock(fromIdx);
    } else idx = -1;
  }
  jsvUnLock2(subStr, newSubStr);
  return str;
}
JsVar *jswrap_string_replace(JsVar *parent, JsVar *subStr, JsVar *newSubStr) {
  return _jswrap_string_replace(parent, subStr, newSubStr, false);
}
JsVar *jswrap_string_replaceAll(JsVar *parent, JsVar *subStr, JsVar *newSubStr) {
  return _jswrap_string_replace(parent, subStr, newSubStr, true);
}
JsVar *jswrap_string_substring(JsVar *parent, JsVarInt pStart, JsVar *vEnd) {
  JsVarInt pEnd = jsvIsUndefined(vEnd) ? (0x7FFFFFFF) : (int)jsvGetInteger(vEnd);
  if (pStart<0) pStart=0;
  if (pEnd<0) pEnd=0;
  if (pEnd<pStart) {
    JsVarInt l = pStart;
    pStart = pEnd;
    pEnd = l;
  }
  return jsvNewFromStringVar(parent, (size_t)pStart, (size_t)(pEnd-pStart));
}
JsVar *jswrap_string_substr(JsVar *parent, JsVarInt pStart, JsVar *vLen) {
  JsVarInt pLen = jsvIsUndefined(vLen) ? (0x7FFFFFFF) : (int)jsvGetInteger(vLen);
  if (pLen<0) pLen = 0;
  if (pStart<0) pStart += (JsVarInt)jsvGetStringLength(parent);
  if (pStart<0) pStart = 0;
  return jsvNewFromStringVar(parent, (size_t)pStart, (size_t)pLen);
}
JsVar *jswrap_string_slice(JsVar *parent, JsVarInt pStart, JsVar *vEnd) {
  JsVarInt pEnd = jsvIsUndefined(vEnd) ? (0x7FFFFFFF) : (int)jsvGetInteger(vEnd);
  if (pStart<0) pStart += (JsVarInt)jsvGetStringLength(parent);
  if (pEnd<0) pEnd += (JsVarInt)jsvGetStringLength(parent);
  if (pStart<0) pStart = 0;
  if (pEnd<0) pEnd = 0;
  if (pEnd<=pStart) return jsvNewFromEmptyString();
  return jsvNewFromStringVar(parent, (size_t)pStart, (size_t)(pEnd-pStart));
}
JsVar *jswrap_string_split(JsVar *parent, JsVar *split) {
  if (!jsvIsString(parent)) return 0;
  JsVar *array = jsvNewEmptyArray();
  if (!array) return 0;
  if (jsvIsUndefined(split)) {
    jsvArrayPush(array, parent);
    return array;
  }
  if (jsvIsInstanceOf(split, "RegExp")) {
    int last = 0;
    JsVar *match;
    jsvObjectSetChildAndUnLock(split, "lastIndex", jsvNewFromInteger(0));
    match = jswrap_regexp_exec(split, parent);
    while (match && !jsvIsNull(match)) {
      JsVar *matchStr = jsvGetArrayItem(match,0);
      JsVarInt idx = jsvObjectGetIntegerChild(match,"index");
      int len = (int)jsvGetStringLength(matchStr);
      jsvUnLock(matchStr);
      jsvArrayPushAndUnLock(array, jsvNewFromStringVar(parent, (size_t)last, (size_t)(idx-last)));
      last = idx+len;
      jsvUnLock(match);
      jsvObjectSetChildAndUnLock(split, "lastIndex", jsvNewFromInteger(last));
      match = jswrap_regexp_exec(split, parent);
    }
    jsvUnLock(match);
    jsvObjectSetChildAndUnLock(split, "lastIndex", jsvNewFromInteger(0));
    if (last <= (int)jsvGetStringLength(parent))
      jsvArrayPushAndUnLock(array, jsvNewFromStringVar(parent, (size_t)last, (0x7FFFFFFF)));
    return array;
  }
  split = jsvAsString(split);
  int idx, last = 0;
  int splitlen = jsvIsUndefined(split) ? 0 : (int)jsvGetStringLength(split);
  int l = (int)jsvGetStringLength(parent) + 1 - splitlen;
  for (idx=0;idx<=l;idx++) {
    if (splitlen==0 && idx==0) continue;
    if (idx==l || splitlen==0 || jsvCompareString(parent, split, (size_t)idx, 0, true)==0) {
      if (idx==l) {
        idx=l+splitlen;
        if (splitlen==0) break;
      }
      JsVar *part = jsvNewFromStringVar(parent, (size_t)jsvConvertFromUTF8Index(parent, last), (size_t)(jsvConvertFromUTF8Index(parent, idx)-jsvConvertFromUTF8Index(parent, last)));
      if (!part) break;
      jsvArrayPush(array, part);
      jsvUnLock(part);
      last = idx+splitlen;
    }
  }
  jsvUnLock(split);
  return array;
}
JsVar *jswrap_string_toUpperLowerCase(JsVar *parent, bool upper) {
  JsVar *res = jsvNewFromEmptyString();
  if (!res) return 0;
  JsVar *parentStr = jsvAsString(parent);
  JsvStringIterator itsrc, itdst;
  jsvStringIteratorNew(&itsrc, parentStr, 0);
  jsvStringIteratorNew(&itdst, res, 0);
  while (jsvStringIteratorHasChar(&itsrc)) {
    char ch = jsvStringIteratorGetCharAndNext(&itsrc);
    ch = upper ? charToUpperCase(ch) : charToLowerCase(ch);
    jsvStringIteratorAppend(&itdst, ch);
  }
  jsvStringIteratorFree(&itsrc);
  jsvStringIteratorFree(&itdst);
  jsvUnLock(parentStr);
  return res;
}
JsVar *jswrap_string_removeAccents(JsVar *parent) {
  bool isLowerCase;
  JsVar *res = jsvNewFromEmptyString();
  if (!res) return 0;
  JsVar *parentStr = jsvAsString(parent);
  JsvStringIterator itsrc, itdst;
  jsvStringIteratorNew(&itsrc, parentStr, 0);
  jsvStringIteratorNew(&itdst, res, 0);
  while (jsvStringIteratorHasChar(&itsrc)) {
    unsigned char ch = (unsigned char)jsvStringIteratorGetCharAndNext(&itsrc);
    if (ch >= 0xE0) {
      isLowerCase = true;
      ch -= 32;
    } else {
      isLowerCase = false;
    }
    if (ch >= 0xC0) {
      switch (ch) {
        case 0xC0 ... 0xC5:
          ch = 'A';
          break;
        case 0xC6:
          jsvStringIteratorAppend(&itdst, isLowerCase ? 'a' : 'A');
          ch = 'E';
          break;
        case 0xC7:
          ch = 'C';
          break;
        case 0xC8 ... 0xCB:
          ch = 'E';
          break;
        case 0xCC ... 0xCF:
          ch = 'I';
          break;
        case 0xD0:
          ch = 'D';
          break;
        case 0xD1:
          ch = 'N';
          break;
        case 0xD2 ... 0xD6:
        case 0xD8:
          ch = 'O';
          break;
        case 0xD9 ... 0xDC:
          ch = 'U';
          break;
        case 0xDD:
          ch = 'Y';
          break;
        case 0xDE:
          ch = 'P';
          break;
        case 0xDF:
          if (isLowerCase) {
            ch = 'Y';
          } else {
            jsvStringIteratorAppend(&itdst, 'S');
            ch = 'S';
          }
          break;
      }
    }
    jsvStringIteratorAppend(&itdst, (char)(isLowerCase ? ch+32 : ch));
  }
  jsvStringIteratorFree(&itsrc);
  jsvStringIteratorFree(&itdst);
  jsvUnLock(parentStr);
  return res;
}
JsVar *jswrap_string_trim(JsVar *parent) {
  JsVar *s = jsvAsString(parent);
  if (!s) return s;
  unsigned int start = 0;
  int end = -1;
  JsvStringIterator it;
  jsvStringIteratorNew(&it, s, 0);
  while (jsvStringIteratorHasChar(&it)) {
    size_t idx = jsvStringIteratorGetIndex(&it);
    bool ws = isWhitespace(jsvStringIteratorGetCharAndNext(&it));
    if (!ws) {
      if (end<0) start = (unsigned int)idx;
      end = (int)idx;
    }
  }
  jsvStringIteratorFree(&it);
  unsigned int len = 0;
  if (end>=(int)start) len = 1+(unsigned int)end-start;
  JsVar *res = jsvNewFromStringVar(s, start, len);
  jsvUnLock(s);
  return res;
}
JsVar *jswrap_string_concat(JsVar *parent, JsVar *args) {
  if (!jsvIsString(parent)) return 0;
  JsVar *str = jsvNewFromStringVarComplete(parent);
  JsVar *extra = jsvArrayJoin(args, NULL , false );
  jsvAppendStringVarComplete(str, extra);
  jsvUnLock(extra);
  return str;
}
bool jswrap_string_startsWith(JsVar *parent, JsVar *search, int position) {
  if (!jsvIsString(parent)) return false;
  JsVar *searchStr = jsvAsString(search);
  bool match = false;
  if (position >= 0 &&
      (int)jsvGetStringLength(searchStr)+position <= (int)jsvGetStringLength(parent))
   match = jsvCompareString(parent, searchStr, (size_t)position,0,true)==0;
  jsvUnLock(searchStr);
  return match;
}
bool jswrap_string_endsWith(JsVar *parent, JsVar *search, JsVar *length) {
  if (!jsvIsString(parent)) return false;
  int position = jsvIsNumeric(length) ? jsvGetInteger(length) : (int)jsvGetStringLength(parent);
  JsVar *searchStr = jsvAsString(search);
  position -= (int)jsvGetStringLength(searchStr);
  bool match = false;
  if (position >= 0 &&
      (int)jsvGetStringLength(searchStr)+position <= (int)jsvGetStringLength(parent))
    match = jsvCompareString(parent, searchStr, (size_t)position,0,true)==0;
  jsvUnLock(searchStr);
  return match;
}
JsVar *jswrap_string_repeat(JsVar *parent, int count) {
  if (count<0) {
    jsExceptionHere(JSET_ERROR, "Invalid count value");
    return 0;
  }
  JsVar *result = jsvNewFromEmptyString();
  while (count-- && !jspIsInterrupted())
    jsvAppendStringVarComplete(result, parent);
  return result;
}
JsVar *jswrap_string_padX(JsVar *str, int targetLength, JsVar *padString, bool padStart) {
  if (!jsvIsString(str) || (int)jsvGetStringLength(str)>=targetLength)
    return jsvLockAgain(str);
  int padChars = targetLength - (int)jsvGetStringLength(str);
  JsVar *result = padStart ? jsvNewFromEmptyString() : jsvNewFromStringVarComplete(str);
  if (!result) return 0;
  padString = padString ? jsvAsString(padString) : jsvNewFromString(" ");
  int padLength = (int)jsvGetStringLength(padString);
  while (padChars > 0) {
    jsvAppendStringVar(result, padString, 0, (size_t)((padLength > padChars) ? padChars : padLength));
    padChars -= padLength;
  }
  if (padStart) jsvAppendStringVarComplete(result, str);
  jsvUnLock(padString);
  return result;
}
static JsVar *jswrap_modules_getModuleList() {
  return jsvObjectGetChild(execInfo.hiddenRoot, "modules", JSV_OBJECT);
}
JsVar *jswrap_require(JsVar *moduleName) {
  if (!jsvIsString(moduleName)) {
    jsExceptionHere(JSET_TYPEERROR, "Expecting module name as a string, got %t", moduleName);
    return 0;
  }
  char moduleNameBuf[128];
  if (jsvGetString(moduleName, moduleNameBuf, sizeof(moduleNameBuf))>=sizeof(moduleNameBuf)) {
    jsExceptionHere(JSET_TYPEERROR, "Module name too long (max 128 chars)");
    return 0;
  }
  JsVar *moduleList = jswrap_modules_getModuleList();
  if (!moduleList) return 0;
  JsVar *moduleExport = jsvSkipNameAndUnLock(jsvFindChildFromString(moduleList, moduleNameBuf));
  jsvUnLock(moduleList);
  if (moduleExport) {
    return moduleExport;
  }
  void *builtInLib = jswGetBuiltInLibrary(moduleNameBuf);
  if (builtInLib) {
    moduleExport = jsvNewNativeFunction(builtInLib, 0);
  }
  if (!moduleExport) {
    const char *builtInJS = jswGetBuiltInJSLibrary(moduleNameBuf);
    if (builtInJS) {
      JsVar *fileContents = jsvNewNativeString((char*)builtInJS, strlen(builtInJS));
      if (fileContents) {
        moduleExport = jspEvaluateModule(fileContents);
        jsvUnLock(fileContents);
      }
    }
  }
  if (moduleExport) {
    JsVar *moduleList = jswrap_modules_getModuleList();
    if (moduleList)
      jsvObjectSetChild(moduleList, moduleNameBuf, moduleExport);
    jsvUnLock(moduleList);
  } else {
    jsExceptionHere(JSET_ERROR, "Module %q not found", moduleName);
  }
  return moduleExport;
}
JsVar *jswrap_modules_getCached() {
  JsVar *arr = jsvNewEmptyArray();
  if (!arr) return 0;
  JsVar *moduleList = jswrap_modules_getModuleList();
  if (!moduleList) return arr;
  JsvObjectIterator it;
  jsvObjectIteratorNew(&it, moduleList);
  while (jsvObjectIteratorHasValue(&it)) {
    JsVar *idx = jsvObjectIteratorGetKey(&it);
    JsVar *idxCopy = jsvCopyNameOnly(idx, false, false);
    jsvArrayPushAndUnLock(arr, idxCopy);
    jsvUnLock(idx);
    jsvObjectIteratorNext(&it);
  }
  jsvObjectIteratorFree(&it);
  jsvUnLock(moduleList);
  return arr;
}
void jswrap_modules_removeCached(JsVar *id) {
  if (!jsvIsString(id)) {
    jsExceptionHere(JSET_ERROR, "First argument must be String");
    return;
  }
  JsVar *moduleList = jswrap_modules_getModuleList();
  if (!moduleList) return;
  JsVar *moduleExportName = jsvFindChildFromVar(moduleList, id, false);
  if (!moduleExportName) {
    jsExceptionHere(JSET_ERROR, "Module %q not found", id);
  } else {
    jsvRemoveChildAndUnLock(moduleList, moduleExportName);
  }
  jsvUnLock(moduleList);
}
void jswrap_modules_removeAllCached() {
  JsVar *moduleList = jswrap_modules_getModuleList();
  if (!moduleList) return;
  jsvRemoveAllChildren(moduleList);
  jsvUnLock(moduleList);
}
void jswrap_modules_addCached(JsVar *id, JsVar *sourceCode) {
  if (!jsvIsString(id) ||
      !(jsvIsString(sourceCode) || jsvIsFunction(sourceCode))) {
    jsExceptionHere(JSET_ERROR, "args must be addCached(string, string|function)");
    return;
  }
  JsVar *moduleList = jswrap_modules_getModuleList();
  if (!moduleList) return;
  JsVar *moduleExport = jspEvaluateModule(sourceCode);
  if (!moduleExport) {
    jsExceptionHere(JSET_ERROR, "Unable to load module %q", id);
  } else {
    jsvObjectSetChildVar(moduleList, id, moduleExport);
    jsvUnLock(moduleExport);
  }
  jsvUnLock(moduleList);
}
static bool isNegativeZero(double x) {
  double NEGATIVE_ZERO = -0.0;
  long long *NEGATIVE_ZERO_BITS = (long long*)&NEGATIVE_ZERO;
  long long *DOUBLE_BITS = (long long*)&x;
  return *DOUBLE_BITS == *NEGATIVE_ZERO_BITS;
}
double jswrap_math_sin(double x) {
  return sin(x);
}
double jswrap_math_cos(double x) {
  return jswrap_math_sin(x + ((3.141592653589793)/2));
}
JsVarFloat jswrap_math_asin(JsVarFloat x) {
  return jswrap_math_atan(x / jswrap_math_sqrt(1-x*x));
}
double jswrap_math_atan(double x) {
  return atan(x);
}
double jswrap_math_atan2(double y, double x) {
  return atan2(y, x);
}
double jswrap_math_mod(double x, double y) {
  double a, b;
  const double c = x;
  if (!isfinite(x) || isnan(y) || y==0)
    return NAN;
  if (y==INFINITY) return x;
  if (0 > c) {
    x = -x;
  }
  if (0 > y) {
    y = -y;
  }
  if (y != 0 && 1.7976931348623157e+308 >= y && 1.7976931348623157e+308 >= x) {
    while (x >= y) {
      a = x / 2;
      b = y;
      while (a >= b) {
        b *= 2;
      }
      x -= b;
    }
  } else {
    x = 0;
  }
  return 0 > c ? -x : x;
}
double jswrap_math_pow(double x, double y) {
  double p;
  int yi = (int)y;
  if (yi>=0 && yi<10 && yi==y) {
    if (yi==0) return 1.0;
    p = x;
    while (yi>1) {
      p *= x;
      yi--;
    }
    return p;
  }
  if (x < 0 && jswrap_math_mod(y, 1) == 0) {
    if (jswrap_math_mod(y, 2) == 0) {
      p = exp(log(-x) * y);
    } else {
      p = -exp(log(-x) * y);
    }
  } else {
    if (x != 0 || 0 >= y) {
      p = exp(log( x) * y);
    } else {
      p = 0;
    }
  }
  return p;
}
JsVar *jswrap_math_round(double x) {
  if (!isfinite(x) || isNegativeZero(x)) return jsvNewFromFloat(x);
  x += (x<0) ? -0.5 : 0.5;
  long long i = (long long)x;
  if (i==0 && (x<0))
    return jsvNewFromFloat(-0.0);
  return jsvNewFromLongInteger(i);
}
double jswrap_math_sqrt(double x) {
  return (x>=0) ? exp(log(x) * 0.5) : NAN;
}
JsVarFloat jswrap_math_clip(JsVarFloat x, JsVarFloat min, JsVarFloat max) {
  if (x<min) x=min;
  if (x>max) x=max;
  return x;
}
JsVarFloat jswrap_math_minmax(JsVar *args, bool isMax) {
  JsVarFloat v = isMax ? -INFINITY : INFINITY;
  JsvObjectIterator it;
  jsvObjectIteratorNew(&it, args);
  while (jsvObjectIteratorHasValue(&it)) {
    JsVarFloat arg = jsvGetFloatAndUnLock(jsvObjectIteratorGetValue(&it));
    if ((isMax && arg > v) || (!isMax && arg < v) || isnan(arg))
      v = arg;
    jsvObjectIteratorNext(&it);
  }
  jsvObjectIteratorFree(&it);
  return v;
}
int jswrap_math_sign(double x)
{
  if (x == 0 || isNegativeZero(x))
    return 0;
  return x > 0 ? 1 : -1;
}
JsVar *jswrap_heatshrink_compress(JsVar *data);
JsVar *jswrap_heatshrink_decompress(JsVar *data);
JsVar *jswrap_heatshrink_compress(JsVar *data) {
  if (!jsvIsIterable(data)) {
    jsExceptionHere(JSET_TYPEERROR,"Expecting something iterable, got %t",data);
    return 0;
  }
  JsvIterator in_it;
  JsvStringIterator out_it;
  jsvIteratorNew(&in_it, data, JSIF_EVERY_ARRAY_ELEMENT);
  uint32_t compressedSize = heatshrink_encode_cb(heatshrink_var_input_cb, (uint32_t*)&in_it, NULL, NULL);
  jsvIteratorFree(&in_it);
  JsVar *outVar = jsvNewStringOfLength((unsigned int)compressedSize, NULL);
  if (!outVar) {
    jsError("Not enough memory for result");
    return 0;
  }
  jsvIteratorNew(&in_it, data, JSIF_EVERY_ARRAY_ELEMENT);
  jsvStringIteratorNew(&out_it,outVar,0);
  heatshrink_encode_cb(heatshrink_var_input_cb, (uint32_t*)&in_it, heatshrink_var_output_cb, (uint32_t*)&out_it);
  jsvStringIteratorFree(&out_it);
  jsvIteratorFree(&in_it);
  JsVar *ab = jsvNewArrayBufferFromString(outVar, 0);
  jsvUnLock(outVar);
  return ab;
}
JsVar *jswrap_heatshrink_decompress(JsVar *data) {
  if (!jsvIsIterable(data)) {
    jsExceptionHere(JSET_TYPEERROR,"Expecting something iterable, got %t",data);
    return 0;
  }
  JsvIterator in_it;
  JsvStringIterator out_it;
  jsvIteratorNew(&in_it, data, JSIF_EVERY_ARRAY_ELEMENT);
  uint32_t decompressedSize = heatshrink_decode(heatshrink_var_input_cb, (uint32_t*)&in_it, NULL);
  jsvIteratorFree(&in_it);
  JsVar *outVar = jsvNewStringOfLength((unsigned int)decompressedSize, NULL);
  if (!outVar) {
    jsError("Not enough memory for result");
    return 0;
  }
  jsvIteratorNew(&in_it, data, JSIF_EVERY_ARRAY_ELEMENT);
  jsvStringIteratorNew(&out_it,outVar,0);
  heatshrink_decode_cb(heatshrink_var_input_cb, (uint32_t*)&in_it, heatshrink_var_output_cb, (uint32_t*)&out_it);
  jsvStringIteratorFree(&out_it);
  jsvIteratorFree(&in_it);
  JsVar *ab = jsvNewArrayBufferFromString(outVar, 0);
  jsvUnLock(outVar);
  return ab;
}
