// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "modules/filesystem/DOMFileSystemBase.h"

#include "core/fileapi/File.h"
#include "public/platform/Platform.h"
#include "public/platform/WebUnitTestSupport.h"
#include <gtest/gtest.h>


namespace blink {

class DOMFileSystemBaseTest : public ::testing::Test {
public:
    DOMFileSystemBaseTest()
    {
        m_filePath = Platform::current()->unitTestSupport()->webKitRootDir();
        m_filePath.append("/Source/modules/filesystem/DOMFileSystemBaseTest.cpp");
        getFileMetadata(m_filePath, m_fileMetadata);
        m_fileMetadata.platformPath = m_filePath;
    }

protected:
    String m_filePath;
    FileMetadata m_fileMetadata;
};


TEST_F(DOMFileSystemBaseTest, externalFilesystemFilesAreUserVisible)
{
    KURL rootUrl = DOMFileSystemBase::createFileSystemRootURL("http://chromium.org/", FileSystemTypeExternal);

    RefPtrWillBeRawPtr<File> file = DOMFileSystemBase::createFile(m_fileMetadata, rootUrl, FileSystemTypeExternal, "DOMFileSystemBaseTest.cpp");
    EXPECT_TRUE(file);
    EXPECT_TRUE(file->hasBackingFile());
    EXPECT_EQ(File::IsUserVisible, file->userVisibility());
    EXPECT_EQ("DOMFileSystemBaseTest.cpp", file->name());
    EXPECT_EQ(m_filePath, file->path());
}

TEST_F(DOMFileSystemBaseTest, temporaryFilesystemFilesAreNotUserVisible)
{
    KURL rootUrl = DOMFileSystemBase::createFileSystemRootURL("http://chromium.org/", FileSystemTypeTemporary);

    RefPtrWillBeRawPtr<File> file = DOMFileSystemBase::createFile(m_fileMetadata, rootUrl, FileSystemTypeTemporary, "UserVisibleName.txt");
    EXPECT_TRUE(file);
    EXPECT_TRUE(file->hasBackingFile());
    EXPECT_EQ(File::IsNotUserVisible, file->userVisibility());
    EXPECT_EQ("UserVisibleName.txt", file->name());
    EXPECT_EQ(m_filePath, file->path());
}

TEST_F(DOMFileSystemBaseTest, persistentFilesystemFilesAreNotUserVisible)
{
    KURL rootUrl = DOMFileSystemBase::createFileSystemRootURL("http://chromium.org/", FileSystemTypePersistent);

    RefPtrWillBeRawPtr<File> file = DOMFileSystemBase::createFile(m_fileMetadata, rootUrl, FileSystemTypePersistent, "UserVisibleName.txt");
    EXPECT_TRUE(file);
    EXPECT_TRUE(file->hasBackingFile());
    EXPECT_EQ(File::IsNotUserVisible, file->userVisibility());
    EXPECT_EQ("UserVisibleName.txt", file->name());
    EXPECT_EQ(m_filePath, file->path());
}

} // namespace blink



