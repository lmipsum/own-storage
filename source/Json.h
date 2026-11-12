// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct json_object;

/**
 * A JSON value on json-c. Copies share the same node, so a member added through an element
 * of an array is seen through every copy of that array.
 */
class Json
{
  public:
    typedef std::vector<Json> Array;

    /** An empty object. */
    Json();
    /** A string value. */
    explicit Json(const std::string &text);
    explicit Json(const char *text);
    /** An array of the given values. */
    explicit Json(const Array &items);
    Json(const Json &other);
    Json &operator=(const Json &other);
    ~Json();

    /** The parsed text, or an empty object when the text is not valid JSON. */
    static Json Parse(const std::string &text);
    /** The parsed file, or an empty object when it is missing or not valid JSON. */
    static Json Load(const std::string &path);
    /** Writes the value to path; false when the file cannot be written. */
    bool Save(const std::string &path) const;

    /** The member called key, or a null value when there is none. */
    Json operator[](const std::string &key) const;
    /** The element at index, or a null value when there is none. */
    Json operator[](std::size_t index) const;
    bool Has(const std::string &key) const;
    /** True for null, an object without members and an array without elements. */
    bool IsEmpty() const;
    /** Sets the member called key, replacing an earlier one. Does nothing unless an object. */
    void Add(const std::string &key, const Json &value);
    /** Adds value at the end. Does nothing unless an array. */
    void Append(const Json &value);

    /** A string's text; any other value as JSON text; "" for null. */
    std::string Str() const;
    /** The elements of an array; empty for any other value. */
    Array AsArray() const;

  private:
    /** Takes over one reference to node. */
    explicit Json(json_object *node);

    json_object *m_node;
};
