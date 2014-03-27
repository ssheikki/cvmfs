/**
 * This file is part of the CernVM File System.
 */

#ifndef CVMFS_LOADER_H_
#define CVMFS_LOADER_H_

#define FUSE_USE_VERSION 26
#define _FILE_OFFSET_BITS 64

#include <stdint.h>
#include <time.h>
#include <fuse/fuse_lowlevel.h>
#include <sys/stat.h>

#include <cstring>
#include <string>
#include <vector>

namespace loader {

extern std::string *usyslog_path_;

enum Failures {
  kFailOk = 0,
  kFailUnknown,
  kFailOptions,
  kFailPermission,
  kFailMount,
  kFailLoaderTalk,
  kFailFuseLoop,
  kFailLoadLibrary,
  kFailIncompatibleVersions,  // TODO
  kFailCacheDir,
  kFailPeers,
  kFailNfsMaps,
  kFailQuota,
  kFailMonitor,
  kFailTalk,
  kFailSignature,
  kFailCatalog,
  kFailMaintenanceMode,
  kFailSaveState,
  kFailRestoreState,
  kFailOtherMount,
  kFailDoubleMount,
  kFailHistory,
  kFailWpad,
};


enum StateId {
  kStateUnknown = 0,
  kStateOpenDirs,
  kStateOpenFiles,
  kStateGlueBuffer,
  kStateInodeGeneration,
  kStateOpenFilesCounter,
  kStateGlueBufferV2,
  kStateGlueBufferV3,
};


struct SavedState {
  SavedState() {
    version = 1;
    size = sizeof(SavedState);
    state_id = kStateUnknown;
    state = NULL;
  }

  uint32_t version;
  uint32_t size;
  StateId state_id;
  void *state;
};
typedef std::vector<SavedState *> StateList;


struct LoadEvent {
  LoadEvent() {
    version = 1;
    size = sizeof(LoadEvent);
    timestamp = 0;
  }

  uint32_t version;
  uint32_t size;
  time_t timestamp;
  std::string so_version;
};
typedef std::vector<LoadEvent *> EventList;


/**
 * This contains the public interface of the cvmfs loader.
 * Whenever something changes, change the version number.
 *
 * Note: Do not forget to check the version of LoaderExports in cvmfs.cc when
 *       using fields that were not present in version 1
 *
 * CernVM-FS 2.1.8 --> Version 2
 */
struct LoaderExports {
  LoaderExports() :
    version(3),
    size(sizeof(LoaderExports)), boot_time(0), foreground(false),
    disable_watchdog(false), shell(0) {}

  uint32_t version;
  uint32_t size;
  time_t boot_time;
  std::string loader_version;
  bool foreground;
  std::string repository_name;
  std::string mount_point;
  std::string config_files;
  std::string program_name;
  EventList history;
  StateList saved_states;

  // added with CernVM-FS 2.1.8 (LoaderExports Version: 2)
  bool disable_watchdog;

  // Version 3
  int shell;
};


struct CvmfsSimpleOps {
  CvmfsSimpleOps() {
    version = 1;
    size = sizeof(CvmfsSimpleOps);
    fnStat = NULL;
    fnLs = NULL;
    fnReadLink = NULL;
    fnGetXattr = NULL;
    fnListXattr = NULL;
    fnOpen = NULL;
    fnRead = NULL;
    fnClose = NULL;
  }

  uint32_t version;
  uint32_t size;

  int (*fnStat)(const std::string &path, struct stat *info);
  int (*fnLs)(const std::string &path, std::vector<std::string> *names);
  int (*fnReadLink)(const std::string &path, const bool resolve_variables,
                    std::string *link);
  int (*fnGetXattr)(const std::string &path, const std::string &attr,
                    std::string *value);
  int (*fnListXattr)(const std::string &path, std::vector<std::string> *names);
  int (*fnOpen)(const std::string &path);
  ssize_t (*fnRead)(const int fd, off_t offset, size_t size, void *buf);
  int (*fnClose)(const int fd);
};


/**
 * This contains the public interface of the cvmfs fuse module.
 * Whenever something changes, change the version number.
 * A global CvmfsExports struct is looked up by the loader via dlsym.
 */
struct CvmfsExports {
  CvmfsExports() {
    version = 2;
    size = sizeof(CvmfsExports);
    fnAltProcessFlavor = NULL;
    fnInit = NULL;
    fnSpawn = NULL;
    fnFini = NULL;
    fnGetErrorMsg = NULL;
    fnMaintenanceMode = NULL;
    fnSaveState = NULL;
    fnRestoreState = NULL;
    fnFreeSavedState = NULL;
    memset(&cvmfs_operations, 0, sizeof(cvmfs_operations));
    simple_ops = NULL;
  }

  uint32_t version;
  uint32_t size;
  std::string so_version;

  int (*fnAltProcessFlavor)(int argc, char **argv);
  int (*fnInit)(const LoaderExports *loader_exports);
  void (*fnSpawn)();
  void (*fnFini)();
  std::string (*fnGetErrorMsg)();
  bool (*fnMaintenanceMode)(const int fd_progress);
  bool (*fnSaveState)(const int fd_progress, StateList *saved_states);
  bool (*fnRestoreState)(const int fd_progress, const StateList &saved_states);
  void (*fnFreeSavedState)(const int fd_progress,
                           const StateList &saved_states);
  struct fuse_lowlevel_ops cvmfs_operations;

  // Version 2
  struct CvmfsSimpleOps *simple_ops;
};

Failures Reload(const int fd_progress, const bool stop_and_go);

}  // namespace loader

#endif  // CVMFS_LOADER_H_
