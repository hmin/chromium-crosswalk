// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/directory_loader.h"

#include "base/callback_helpers.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_ptr.h"
#include "base/prefs/testing_pref_service.h"
#include "base/run_loop.h"
#include "chrome/browser/chromeos/drive/change_list_loader.h"
#include "chrome/browser/chromeos/drive/change_list_loader_observer.h"
#include "chrome/browser/chromeos/drive/file_cache.h"
#include "chrome/browser/chromeos/drive/file_system_util.h"
#include "chrome/browser/chromeos/drive/job_scheduler.h"
#include "chrome/browser/chromeos/drive/resource_metadata.h"
#include "chrome/browser/chromeos/drive/test_util.h"
#include "chrome/browser/drive/event_logger.h"
#include "chrome/browser/drive/fake_drive_service.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "google_apis/drive/drive_api_parser.h"
#include "google_apis/drive/test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace drive {
namespace internal {

namespace {

class TestDirectoryLoaderObserver : public ChangeListLoaderObserver {
 public:
  explicit TestDirectoryLoaderObserver(DirectoryLoader* loader)
      : loader_(loader) {
    loader_->AddObserver(this);
  }

  virtual ~TestDirectoryLoaderObserver() {
    loader_->RemoveObserver(this);
  }

  const std::set<base::FilePath>& changed_directories() const {
    return changed_directories_;
  }
  void clear_changed_directories() { changed_directories_.clear(); }

  // ChageListObserver overrides:
  virtual void OnDirectoryChanged(
      const base::FilePath& directory_path) OVERRIDE {
    changed_directories_.insert(directory_path);
  }

 private:
  DirectoryLoader* loader_;
  std::set<base::FilePath> changed_directories_;

  DISALLOW_COPY_AND_ASSIGN(TestDirectoryLoaderObserver);
};

void AccumulateReadDirectoryResult(FileError* out_error,
                                   ResourceEntryVector* out_entries,
                                   bool* last_has_more,
                                   FileError error,
                                   scoped_ptr<ResourceEntryVector> entries,
                                   bool has_more) {
  EXPECT_TRUE(*last_has_more);
  *out_error = error;
  *last_has_more = has_more;
  if (error == FILE_ERROR_OK) {
    ASSERT_TRUE(entries);
    out_entries->insert(out_entries->end(), entries->begin(), entries->end());
  } else {
    EXPECT_FALSE(has_more);
  }
}

}  // namespace

class DirectoryLoaderTest : public testing::Test {
 protected:
  virtual void SetUp() OVERRIDE {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    pref_service_.reset(new TestingPrefServiceSimple);
    test_util::RegisterDrivePrefs(pref_service_->registry());

    logger_.reset(new EventLogger);

    drive_service_.reset(new FakeDriveService);
    ASSERT_TRUE(drive_service_->LoadResourceListForWapi(
        "gdata/root_feed.json"));
    ASSERT_TRUE(drive_service_->LoadAccountMetadataForWapi(
        "gdata/account_metadata.json"));

    scheduler_.reset(new JobScheduler(pref_service_.get(),
                                      logger_.get(),
                                      drive_service_.get(),
                                      base::MessageLoopProxy::current().get()));
    metadata_storage_.reset(new ResourceMetadataStorage(
        temp_dir_.path(), base::MessageLoopProxy::current().get()));
    ASSERT_TRUE(metadata_storage_->Initialize());

    metadata_.reset(new ResourceMetadata(
        metadata_storage_.get(), base::MessageLoopProxy::current().get()));
    ASSERT_EQ(FILE_ERROR_OK, metadata_->Initialize());

    cache_.reset(new FileCache(metadata_storage_.get(),
                               temp_dir_.path(),
                               base::MessageLoopProxy::current().get(),
                               NULL /* free_disk_space_getter */));
    ASSERT_TRUE(cache_->Initialize());

    about_resource_loader_.reset(new AboutResourceLoader(scheduler_.get()));
    loader_controller_.reset(new LoaderController);
    directory_loader_.reset(
        new DirectoryLoader(logger_.get(),
                             base::MessageLoopProxy::current().get(),
                             metadata_.get(),
                             scheduler_.get(),
                             drive_service_.get(),
                             about_resource_loader_.get(),
                             loader_controller_.get()));
  }

  // Adds a new file to the root directory of the service.
  scoped_ptr<google_apis::ResourceEntry> AddNewFile(const std::string& title) {
    google_apis::GDataErrorCode error = google_apis::GDATA_FILE_ERROR;
    scoped_ptr<google_apis::ResourceEntry> entry;
    drive_service_->AddNewFile(
        "text/plain",
        "content text",
        drive_service_->GetRootResourceId(),
        title,
        false,  // shared_with_me
        google_apis::test_util::CreateCopyResultCallback(&error, &entry));
    base::RunLoop().RunUntilIdle();
    EXPECT_EQ(google_apis::HTTP_CREATED, error);
    return entry.Pass();
  }

  content::TestBrowserThreadBundle thread_bundle_;
  base::ScopedTempDir temp_dir_;
  scoped_ptr<TestingPrefServiceSimple> pref_service_;
  scoped_ptr<EventLogger> logger_;
  scoped_ptr<FakeDriveService> drive_service_;
  scoped_ptr<JobScheduler> scheduler_;
  scoped_ptr<ResourceMetadataStorage,
             test_util::DestroyHelperForTests> metadata_storage_;
  scoped_ptr<ResourceMetadata, test_util::DestroyHelperForTests> metadata_;
  scoped_ptr<FileCache, test_util::DestroyHelperForTests> cache_;
  scoped_ptr<AboutResourceLoader> about_resource_loader_;
  scoped_ptr<LoaderController> loader_controller_;
  scoped_ptr<DirectoryLoader> directory_loader_;
};

TEST_F(DirectoryLoaderTest, ReadDirectory_GrandRoot) {
  TestDirectoryLoaderObserver observer(directory_loader_.get());

  // Load grand root.
  FileError error = FILE_ERROR_FAILED;
  ResourceEntryVector entries;
  bool last_has_more = true;
  directory_loader_->ReadDirectory(
      util::GetDriveGrandRootPath(),
      base::Bind(&AccumulateReadDirectoryResult,
                 &error, &entries, &last_has_more));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(FILE_ERROR_OK, error);
  EXPECT_FALSE(last_has_more);
  EXPECT_EQ(0U, observer.changed_directories().size());
  observer.clear_changed_directories();

  // My Drive has resource ID.
  ResourceEntry entry;
  EXPECT_EQ(FILE_ERROR_OK,
            metadata_->GetResourceEntryByPath(util::GetDriveMyDriveRootPath(),
                                              &entry));
  EXPECT_EQ(drive_service_->GetRootResourceId(), entry.resource_id());
}

TEST_F(DirectoryLoaderTest, ReadDirectory_MyDrive) {
  TestDirectoryLoaderObserver observer(directory_loader_.get());

  // My Drive does not have resource ID yet.
  ResourceEntry entry;
  EXPECT_EQ(FILE_ERROR_OK,
            metadata_->GetResourceEntryByPath(util::GetDriveMyDriveRootPath(),
                                              &entry));
  EXPECT_TRUE(entry.resource_id().empty());

  // Load My Drive.
  FileError error = FILE_ERROR_FAILED;
  ResourceEntryVector entries;
  bool last_has_more = true;
  directory_loader_->ReadDirectory(
      util::GetDriveMyDriveRootPath(),
      base::Bind(&AccumulateReadDirectoryResult,
                 &error, &entries, &last_has_more));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(FILE_ERROR_OK, error);
  EXPECT_FALSE(last_has_more);
  EXPECT_EQ(1U, observer.changed_directories().count(
      util::GetDriveMyDriveRootPath()));

  // My Drive has resource ID.
  EXPECT_EQ(FILE_ERROR_OK,
            metadata_->GetResourceEntryByPath(util::GetDriveMyDriveRootPath(),
                                              &entry));
  EXPECT_EQ(drive_service_->GetRootResourceId(), entry.resource_id());
  EXPECT_EQ(drive_service_->about_resource().largest_change_id(),
            entry.directory_specific_info().changestamp());

  // My Drive's child is present.
  base::FilePath file_path =
      util::GetDriveMyDriveRootPath().AppendASCII("File 1.txt");
  EXPECT_EQ(FILE_ERROR_OK,
            metadata_->GetResourceEntryByPath(file_path, &entry));
}

TEST_F(DirectoryLoaderTest, ReadDirectory_MultipleCalls) {
  TestDirectoryLoaderObserver observer(directory_loader_.get());

  // Load grand root.
  FileError error = FILE_ERROR_FAILED;
  ResourceEntryVector entries;
  bool last_has_more = true;
  directory_loader_->ReadDirectory(
      util::GetDriveGrandRootPath(),
      base::Bind(&AccumulateReadDirectoryResult,
                 &error, &entries, &last_has_more));

  // Load grand root again without waiting for the result.
  FileError error2 = FILE_ERROR_FAILED;
  ResourceEntryVector entries2;
  bool last_has_more2 = true;
  directory_loader_->ReadDirectory(
      util::GetDriveGrandRootPath(),
      base::Bind(&AccumulateReadDirectoryResult,
                 &error2, &entries2, &last_has_more2));
  base::RunLoop().RunUntilIdle();

  // Callback is called for each method call.
  EXPECT_EQ(FILE_ERROR_OK, error);
  EXPECT_FALSE(last_has_more);
  EXPECT_EQ(FILE_ERROR_OK, error2);
  EXPECT_FALSE(last_has_more2);
}

TEST_F(DirectoryLoaderTest, Lock) {
  // Lock the loader.
  scoped_ptr<base::ScopedClosureRunner> lock = loader_controller_->GetLock();

  // Start loading.
  TestDirectoryLoaderObserver observer(directory_loader_.get());
  FileError error = FILE_ERROR_FAILED;
  ResourceEntryVector entries;
  bool last_has_more = true;
  directory_loader_->ReadDirectory(
      util::GetDriveMyDriveRootPath(),
      base::Bind(&AccumulateReadDirectoryResult,
                 &error, &entries, &last_has_more));
  base::RunLoop().RunUntilIdle();

  // Update is pending due to the lock.
  EXPECT_TRUE(observer.changed_directories().empty());

  // Unlock the loader, this should resume the pending udpate.
  lock.reset();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1U, observer.changed_directories().count(
      util::GetDriveMyDriveRootPath()));
}

}  // namespace internal
}  // namespace drive
