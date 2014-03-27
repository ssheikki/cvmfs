/**
 * This file is part of the CernVM File System.
 */

#ifndef CVMFS_SHELL_H_
#define CVMFS_SHELL_H_

#include <string>
#include "loader.h"
#include "platform.h"

namespace shell {

class Shell {
 public:
  Shell();
  int Enter(struct loader::LoaderExports *loader_exports,
            struct loader::CvmfsExports *cvmfs_exports);
 private:
  bool Prompt();
  bool Stat(const std::string &path, loader::platform_stat64 *stat);
  void Ls();

  struct loader::LoaderExports *loader_exports_;
  struct loader::CvmfsExports *cvmfs_exports_;
  std::string cwd_;
};

}  // namespace shell

#endif  // CVMFS_SHELL_H_
