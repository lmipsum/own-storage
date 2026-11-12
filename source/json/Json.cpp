// SPDX-License-Identifier: GPL-3.0-or-later
#include "Json.h"

#include <json-c/json.h>

#include <cstdio>

Json::Json() : m_node(json_object_new_object()) {}

Json::Json(const std::string &text) : m_node(json_object_new_string(text.c_str())) {}

Json::Json(const char *text) : m_node(json_object_new_string(text)) {}

Json::Json(const Array &items) : m_node(json_object_new_array())
{
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        json_object_array_add(m_node, json_object_get(items[i].m_node));
    }
}

Json::Json(json_object *node) : m_node(node) {}

Json::Json(const Json &other) : m_node(json_object_get(other.m_node)) {}

Json &Json::operator=(const Json &other)
{
    json_object *previous = m_node;
    m_node = json_object_get(other.m_node);
    json_object_put(previous);
    return *this;
}

Json::~Json()
{
    json_object_put(m_node);
}

Json Json::Parse(const std::string &text)
{
    json_object *node = json_tokener_parse(text.c_str());
    if (node == NULL)
    {
        return Json();
    }
    return Json(node);
}

Json Json::Load(const std::string &path)
{
    FILE *file = std::fopen(path.c_str(), "rb");
    if (file == NULL)
    {
        return Json();
    }
    std::string text;
    char buffer[4096];
    std::size_t count;
    while ((count = std::fread(buffer, 1, sizeof buffer, file)) > 0)
    {
        text.append(buffer, count);
    }
    std::fclose(file);
    return Parse(text);
}

bool Json::Save(const std::string &path) const
{
    FILE *file = std::fopen(path.c_str(), "wb");
    if (file == NULL)
    {
        return false;
    }
    const std::string text = Str();
    const bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    return std::fclose(file) == 0 && written;
}

Json Json::operator[](const std::string &key) const
{
    json_object *member = NULL;
    if (!json_object_is_type(m_node, json_type_object) ||
        !json_object_object_get_ex(m_node, key.c_str(), &member))
    {
        return Json(static_cast<json_object *>(NULL));
    }
    return Json(json_object_get(member));
}

Json Json::operator[](std::size_t index) const
{
    if (!json_object_is_type(m_node, json_type_array) ||
        index >= static_cast<std::size_t>(json_object_array_length(m_node)))
    {
        return Json(static_cast<json_object *>(NULL));
    }
    return Json(json_object_get(json_object_array_get_idx(m_node, static_cast<int>(index))));
}

bool Json::Has(const std::string &key) const
{
    return json_object_is_type(m_node, json_type_object) &&
           json_object_object_get_ex(m_node, key.c_str(), NULL);
}

bool Json::IsEmpty() const
{
    if (json_object_is_type(m_node, json_type_object))
    {
        return json_object_get_object(m_node)->count == 0;
    }
    if (json_object_is_type(m_node, json_type_array))
    {
        return json_object_array_length(m_node) == 0;
    }
    return m_node == NULL;
}

void Json::Add(const std::string &key, const Json &value)
{
    if (json_object_is_type(m_node, json_type_object))
    {
        json_object_object_add(m_node, key.c_str(), json_object_get(value.m_node));
    }
}

void Json::Append(const Json &value)
{
    if (json_object_is_type(m_node, json_type_array))
    {
        json_object_array_add(m_node, json_object_get(value.m_node));
    }
}

std::string Json::Str() const
{
    const char *text = json_object_get_string(m_node);
    return text == NULL ? std::string() : std::string(text);
}

Json::Array Json::AsArray() const
{
    Array items;
    if (json_object_is_type(m_node, json_type_array))
    {
        const int length = json_object_array_length(m_node);
        for (int i = 0; i < length; ++i)
        {
            items.push_back(Json(json_object_get(json_object_array_get_idx(m_node, i))));
        }
    }
    return items;
}
