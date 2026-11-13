// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "auth/AuthorizedQueue.h"
#include "json/Json.h"

#include <cstddef>
#include <string>

const std::string jsonMime = "application/json";
const std::string folderMime = "application/vnd.google-apps.folder";
const std::string octetMime = "application/octet-stream";
// Drive takes metadata and content in one request up to 5 MB; bigger files go up in a
// resumable session.
const long multipartUploadLimit = 5 * 1024 * 1024;
// Separates metadata and content in a multipart upload; a file that holds it goes up resumable.
const std::string multipartBoundary = "own_storage_part_7d1c4f0b9a2e";

std::string readFirstErrorReason(const Json &error);
std::string describeDriveError(const std::string &reply);
bool isDriveFullError(const std::string &reply);
Json::Array getResourcesUnderFolder(std::string parentId);
Json findFirstRemoteItem(const std::string &query);
std::string buildTitleInFolderQuery(const std::string &title, const std::string &parentId);
Json checkIfRemoteResourceExists(std::string resourceTitle, std::string mimeType,
                                 std::string parentId);
std::string constructMetaData(std::string title, std::string mimeType, std::string parentid);
bool downloadFile(std::string localPath, std::string fileId);
bool readWholeFile(const std::string &path, std::string &text);
QueuedRequest buildMultipartRequest(const std::string &meta, const std::string &mimeType,
                                    const std::string &content, const std::string &fileId);
Json uploadMultipart(const std::string &meta, const std::string &mimeType,
                     const std::string &content, const std::string &fileId,
                     const std::string &path);
Json uploadFile(std::string path, std::string filename, std::string mimeType, std::string parentId,
                std::string fileId = "");
Json uploadDirectory(std::string dirName, std::string parentId);
