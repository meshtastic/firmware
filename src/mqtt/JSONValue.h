/*
 * File JSONValue.h part of the SimpleJSON Library - http://mjpa.in/json
 *
 * Copyright (C) 2010 Mike Anchor
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

#ifndef _JSONVALUE_H_
#define _JSONVALUE_H_

#include <string>
#include <vector>

#include "JSON.h"

class JSON;

enum JSONType { JSONType_Null, JSONType_String, JSONType_Bool, JSONType_Number, JSONType_Array, JSONType_Object };

class JSONValue
{
    friend class JSON;

  public:
    JSONValue(/*NULL*/);
    JSONValue(const char *m_char_value);
    JSONValue(const std::string &m_string_value);
    JSONValue(bool m_bool_value);
    JSONValue(double m_number_value);
    JSONValue(int m_integer_value);
    JSONValue(uint m_integer_value);
    JSONValue(const JSONArray &m_array_value);
    JSONValue(const JSONObject &m_object_value);
    JSONValue(const JSONValue &m_source);
    ~JSONValue();

    bool IsNull() const;
    bool IsString() const;
    bool IsBool() const;
    bool IsNumber() const;
    bool IsArray() const;
    bool IsObject() const;

    const std::string &AsString() const;
    bool AsBool() const;
    double AsNumber() const;
    const JSONArray &AsArray() const;
    const JSONObject &AsObject() const;

    std::size_t CountChildren() const;
    bool HasChild(std::size_t index) const;
    JSONValue *Child(std::size_t index);
    bool HasChild(const char *name) const;
    JSONValue *Child(const char *name);
    std::vector<std::string> ObjectKeys() const;

    std::string Stringify(bool const prettyprint = false) const;

  protected:
    static JSONValue *Parse(const char **data);

  private:
    static std::string StringifyString(const std::string &str);
    std::string StringifyImpl(size_t const indentDepth) const;
    static std::string Indent(size_t depth);

    JSONType type;

    union {
        bool bool_value;
        double number_value;
        std::string *string_value;
        JSONArray *array_value;
        JSONObject *object_value;
    };
};

#endif