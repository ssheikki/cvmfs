#include <gtest/gtest.h>
#include <string>
#include <map>
#include <cassert>
#include <iostream> // TODO: remove me!
#include "../../cvmfs/prng.h"

#include "../../cvmfs/catalog_traversal.h"
#include "../../cvmfs/manifest.h"
#include "../../cvmfs/hash.h"

using namespace swissknife;

static const std::string rhs       = "f9d87ae2cc46be52b324335ff05fae4c1a7c4dd4";
static const shash::Any  root_hash = shash::Any(shash::kSha1, shash::HexPtr(rhs));

/**
 * This is a mock of an ObjectFetcher that does essentially nothing.
 */
class MockObjectFetcher {
 public:
  MockObjectFetcher(const CatalogTraversalParams &params) {}
 public:
  manifest::Manifest* FetchManifest() {
    return new manifest::Manifest(root_hash, 0, "");
  }
  inline bool Fetch(const shash::Any  &catalog_hash,
                    std::string       *catalog_file) {
    return true;
  }
  inline bool Exists(const std::string &file) {
    return false;
  }
};


/**
 * This is a minimal mock of a Catalog class.
 */
class MockCatalog {
 public:
  typedef std::map<shash::Any, MockCatalog*> AvailableCatalogs;
  static AvailableCatalogs available_catalogs;
  static unsigned int      instances;

  static void Reset() {
    MockCatalog::instances = 0;
    MockCatalog::UnregisterCatalogs();
  }

  static void RegisterCatalog(MockCatalog *catalog) {
    ASSERT_EQ (MockCatalog::available_catalogs.end(),
               MockCatalog::available_catalogs.find(catalog->catalog_hash()));
    MockCatalog::available_catalogs[catalog->catalog_hash()] = catalog;
  }

  static void UnregisterCatalogs() {
    MockCatalog::AvailableCatalogs::const_iterator i, iend;
    for (i    = MockCatalog::available_catalogs.begin(),
         iend = MockCatalog::available_catalogs.end();
         i != iend; ++i)
    {
      delete i->second;
    }
    MockCatalog::available_catalogs.clear();
  }

  static MockCatalog* GetCatalog(const shash::Any &catalog_hash) {
    AvailableCatalogs::const_iterator clg_itr =
      MockCatalog::available_catalogs.find(catalog_hash);
    return (MockCatalog::available_catalogs.end() != clg_itr)
      ? clg_itr->second
      : NULL;
  }

 public:
  struct NestedCatalog {
    PathString   path;
    shash::Any   hash;
    MockCatalog *child;
    uint64_t     size;
  };
  typedef std::vector<NestedCatalog> NestedCatalogList;

 public:
  MockCatalog(const std::string &root_path,
              const shash::Any  &catalog_hash,
              const uint64_t     catalog_size,
              const unsigned int revision,
              const bool         is_root,
              MockCatalog *parent   = NULL,
              MockCatalog *previous = NULL) :
    parent_(parent), previous_(previous), root_path_(root_path),
    catalog_hash_(catalog_hash), catalog_size_(catalog_size),
    revision_(revision), is_root_(is_root)
  {
    if (parent != NULL) {
      parent->RegisterChild(this);
    }
    ++MockCatalog::instances;
  }

  MockCatalog(const MockCatalog &other) :
    parent_(other.parent_), previous_(other.previous_),
    root_path_(other.root_path_), catalog_hash_(other.catalog_hash_),
    catalog_size_(other.catalog_size_), revision_(other.revision_),
    is_root_(other.is_root_), children_(other.children_)
  {
    ++MockCatalog::instances;
  }

  ~MockCatalog() {
    --MockCatalog::instances;
  }

 public: /* API in this 'public block' is used by CatalogTraversal
          * (see catalog.h - catalog::Catalog for details)
          */
  static MockCatalog* AttachFreely(const std::string  &root_path,
                                   const std::string  &file,
                                   const shash::Any   &catalog_hash,
                                         MockCatalog  *parent = NULL) {
    const MockCatalog *catalog = MockCatalog::GetCatalog(catalog_hash);
    if (catalog == NULL) {
      return NULL;
    } else {
      MockCatalog *new_catalog = catalog->Clone();
      new_catalog->set_parent(parent);
      return new_catalog;
    }
  }

  bool IsRoot() const { return is_root_; }

  const NestedCatalogList& ListNestedCatalogs() const { return children_; }

  unsigned int GetRevision() const { return revision_; }

  shash::Any GetPreviousRevision() const {
    return (previous_ != NULL) ? previous_->catalog_hash() : shash::Any();
  }

 public:
  const PathString   path()         const { return PathString(root_path_);  }
  const std::string& root_path()    const { return root_path_;              }
  const shash::Any&  catalog_hash() const { return catalog_hash_;           }
  uint64_t           catalog_size() const { return catalog_size_;           }
  unsigned int       revision()     const { return revision_;               }

  MockCatalog*       parent()       const { return parent_;                 }
  MockCatalog*       previous()     const { return previous_;               }

  void set_parent(MockCatalog *parent) { parent_ = parent; }

 public:
  void RegisterChild(MockCatalog *child) {
    NestedCatalog nested;
    nested.path  = PathString(child->root_path());
    nested.hash  = child->catalog_hash();
    nested.child = child;
    nested.size  = child->catalog_size();
    children_.push_back(nested);
  }

 protected:
  MockCatalog* Clone() const {
    return new MockCatalog(*this);
  }

 private:
  MockCatalog        *parent_;
  MockCatalog        *previous_;
  const std::string   root_path_;
  const shash::Any    catalog_hash_;
  const uint64_t      catalog_size_;
  const unsigned int  revision_;
  const bool          is_root_;

  NestedCatalogList   children_;
};

MockCatalog::AvailableCatalogs MockCatalog::available_catalogs;
unsigned int                   MockCatalog::instances;


//
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//

typedef CatalogTraversal<MockCatalog, MockObjectFetcher> MockedCatalogTraversal;
typedef std::pair<unsigned int, std::string>             CatalogIdentifier;
typedef std::vector<CatalogIdentifier>                   CatalogIdentifiers;

class T_CatalogTraversal : public ::testing::Test {
 public:
  MockCatalog *dummy_catalog_hierarchy;

 protected:
  typedef std::map<std::string, MockCatalog*>    CatalogPathMap;
  typedef std::map<unsigned int, CatalogPathMap> RevisionMap;

  const unsigned int max_revision;
  const unsigned int intial_catalog_instances;

 public:
  T_CatalogTraversal() :
    max_revision(6),
    intial_catalog_instances(42) /* depends on max_revision */ {}

 protected:
  void SetUp() {
    dice_.InitLocaltime();
    MockCatalog::Reset();
    SetupDummyCatalogs();
    EXPECT_EQ (intial_catalog_instances, MockCatalog::instances);
  }

  void TearDown() {
    MockCatalog::UnregisterCatalogs();
    EXPECT_EQ (0u, MockCatalog::instances);
  }

  void CheckVisitedCatalogs(const CatalogIdentifiers expected,
                            const CatalogIdentifiers observed) {
    EXPECT_EQ (expected.size(), observed.size());
    typedef CatalogIdentifiers::const_iterator itr;

    unsigned int _i = 0;
    itr i    = expected.begin();
    itr iend = expected.end();
    for (; i != iend; ++i) {
      bool found = false;

      unsigned int _j = 0;
      itr j    = observed.begin();
      itr jend = observed.end();
      for (; j != jend; ++j) {
        if (*i == *j) {
          found = true;
          EXPECT_EQ (_i, _j) << "traversing order changed";
          break;
        }
      }

      EXPECT_TRUE (found) << "didn't find catalog: " << i->second << " "
                          << "(revision: " << i->first << ")";
    }
  }

 private:
  CatalogPathMap& GetCatalogTree(const unsigned int  revision,
                                 RevisionMap        &revisions) const {
    RevisionMap::iterator rev_itr = revisions.find(revision);
    assert (rev_itr != revisions.end());
    return rev_itr->second;
  }

  MockCatalog* GetRevisionHead(const unsigned int revision,
                               RevisionMap &revisions) const {
    CatalogPathMap &catalogs = GetCatalogTree(revision, revisions);
    CatalogPathMap::iterator catalogs_itr = catalogs.find("");
    assert (catalogs_itr != catalogs.end());
    assert (catalogs_itr->second->revision() == revision);
    assert (catalogs_itr->second->IsRoot());
    return catalogs_itr->second;
  }

  MockCatalog* GetBranchHead(const std::string   &root_path,
                             const unsigned int   revision,
                             RevisionMap         &revisions) const {
    CatalogPathMap &catalogs = GetCatalogTree(revision, revisions);
    CatalogPathMap::iterator catalogs_itr = catalogs.find(root_path);
    assert (catalogs_itr != catalogs.end());
    assert (catalogs_itr->second->revision()  == revision);
    assert (catalogs_itr->second->root_path() == root_path);
    return catalogs_itr->second;
  }

  void SetupDummyCatalogs() {
    /**
     * Dummy catalog hierarchy:
     *
     *  0-0 HEAD
     *   |
     *   +-------------------+---------------+---------------+
     *   |                   |               |               |
     *  1-0                 1-1             1-2             1-3
     *   |                   |               |               |
     *   +-------------+     +----+----+     +----+----+     +----+
     *   |             |     |    |    |     |    |    |     |    |
     *  2-0           2-1   2-2  2-3  2-4   2-5  2-6  2-7   2-8  2-9
     *   |                   |                    |
     *   +----+----+         +-----+              +-----+-----+-----+
     *   |    |    |         |     |              |     |     |     |
     *  3-0  3-1  3-2       3-3   3-4            3-5   3-6   3-7   3-8
     *   |                         |
     *   |                         +-----+-----+
     *   |                         |     |     |
     *  4-0                       4-1   4-2   4-3
     *
     * Parts of the hierarchy are created multiple times in order to get some
     * historic catalogs. To simplify the creation the history looks like so:
     *                                                                # catalogs
     *    Revision 1:   - only the root catalog (0-0)                       1
     *    Revision 2:   - adds branch 1-0                                   8
     *    Revision 3:   - adds branch 1-1                                  17
     *    Revision 4:   - adds branch 1-2 and branch 1-1 is recreated      25
     *    Revision 5:   - adds branch 1-3                                  28
     *    Revision 6:   - removes branch 1-0                               21
     *
     */

    RevisionMap revisions;
    for (unsigned int r = 1; r <= max_revision; ++r) {
      MakeRevision(r, revisions);
    }
  }

  void MakeRevision(const unsigned int revision, RevisionMap &revisions) {
    // sanity checks
    RevisionMap::const_iterator rev_itr = revisions.find(revision);
    ASSERT_EQ (revisions.end(), rev_itr);
    ASSERT_LE (1u, revision);
    ASSERT_GE (max_revision, revision);

    // create map for new catalog tree
    revisions[revision] = CatalogPathMap();

    // create the root catalog
    MockCatalog *root_catalog = (revision < max_revision)
      ? CreateAndRegisterCatalog("", revision, revisions)
      : CreateAndRegisterCatalog("", revision, revisions, NULL, root_hash);

    // create the catalog hierarchy depending on the revision
    switch (revision) {
      case 1:
        // NOOP
        break;
      case 2:
        MakeBranch("/00/10", revision, revisions);
        break;
      case 3:
        MakeBranch("/00/11", revision, revisions);
        root_catalog->RegisterChild(GetBranchHead("/00/10", 2, revisions));
        break;
      case 4:
        MakeBranch("/00/12", revision, revisions);
        MakeBranch("/00/11", revision, revisions);
        root_catalog->RegisterChild(GetBranchHead("/00/10", 2, revisions));
        break;
      case 5:
        MakeBranch("/00/13", revision, revisions);
        root_catalog->RegisterChild(GetBranchHead("/00/10", 2, revisions));
        root_catalog->RegisterChild(GetBranchHead("/00/11", 4, revisions));
        root_catalog->RegisterChild(GetBranchHead("/00/12", 4, revisions));
        break;
      case 6:
        root_catalog->RegisterChild(GetBranchHead("/00/11", 4, revisions));
        root_catalog->RegisterChild(GetBranchHead("/00/12", 4, revisions));
        root_catalog->RegisterChild(GetBranchHead("/00/13", 5, revisions));
        dummy_catalog_hierarchy = root_catalog; // sets current repo HEAD
        break;
      default:
        FAIL() << "hit revision: " << revision;
    }
  }

  void MakeBranch(const std::string &branch, const unsigned int revision, RevisionMap &revisions) {
    MockCatalog *revision_root = GetRevisionHead(revision, revisions);

    if (branch == "/00/10") {
      MockCatalog *_10 = CreateAndRegisterCatalog("/00/10",          revision, revisions, revision_root);
      MockCatalog *_20 = CreateAndRegisterCatalog("/00/10/20",       revision, revisions,           _10);
                         CreateAndRegisterCatalog("/00/10/21",       revision, revisions,           _10);
      MockCatalog *_30 = CreateAndRegisterCatalog("/00/10/20/30",    revision, revisions,           _20);
                         CreateAndRegisterCatalog("/00/10/20/31",    revision, revisions,           _20);
                         CreateAndRegisterCatalog("/00/10/20/32",    revision, revisions,           _20);
                         CreateAndRegisterCatalog("/00/10/20/30/40", revision, revisions,           _30);

    } else if (branch == "/00/11") {
      MockCatalog *_11 = CreateAndRegisterCatalog("/00/11",          revision, revisions, revision_root);
      MockCatalog *_22 = CreateAndRegisterCatalog("/00/11/22",       revision, revisions,           _11);
                         CreateAndRegisterCatalog("/00/11/23",       revision, revisions,           _11);
                         CreateAndRegisterCatalog("/00/11/24",       revision, revisions,           _11);
                         CreateAndRegisterCatalog("/00/11/22/33",    revision, revisions,           _22);
      MockCatalog *_34 = CreateAndRegisterCatalog("/00/11/22/34",    revision, revisions,           _22);
                         CreateAndRegisterCatalog("/00/11/22/34/41", revision, revisions,           _34);
                         CreateAndRegisterCatalog("/00/11/22/34/42", revision, revisions,           _34);
                         CreateAndRegisterCatalog("/00/11/22/34/43", revision, revisions,           _34);

    } else if (branch == "/00/12") {
      MockCatalog *_12 = CreateAndRegisterCatalog("/00/12",          revision, revisions, revision_root);
                         CreateAndRegisterCatalog("/00/12/25",       revision, revisions,           _12);
      MockCatalog *_26 = CreateAndRegisterCatalog("/00/12/26",       revision, revisions,           _12);
                         CreateAndRegisterCatalog("/00/12/27",       revision, revisions,           _12);
                         CreateAndRegisterCatalog("/00/12/26/35",    revision, revisions,           _26);
                         CreateAndRegisterCatalog("/00/12/26/36",    revision, revisions,           _26);
                         CreateAndRegisterCatalog("/00/12/26/37",    revision, revisions,           _26);
                         CreateAndRegisterCatalog("/00/12/26/38",    revision, revisions,           _26);

    } else if (branch == "/00/13") {
      MockCatalog *_13 = CreateAndRegisterCatalog("/00/13",          revision, revisions, revision_root);
                         CreateAndRegisterCatalog("/00/13/28",       revision, revisions,          _13);
                         CreateAndRegisterCatalog("/00/13/29",       revision, revisions,          _13);

    } else {
      FAIL();
    }
  }

  MockCatalog* CreateAndRegisterCatalog(
                  const std::string  &root_path,
                  const unsigned int  revision,
                  RevisionMap        &revisions,
                  MockCatalog        *parent       = NULL,
                  const shash::Any   &catalog_hash = shash::Any(shash::kSha1)) {
    // produce a random hash if no catalog has was given
    shash::Any effective_clg_hash = catalog_hash;
    if (effective_clg_hash.IsNull()) {
      effective_clg_hash.Randomize(dice_);
    }

    // get catalog tree for current revision
    CatalogPathMap &catalogs = GetCatalogTree(revision, revisions);

    // find previous catalog from the RevisionsMaps (if there is one)
    MockCatalog *previous_catalog = NULL;
    if (revision > 1) {
      RevisionMap::iterator prev_rev_itr = revisions.find(revision - 1);
      assert (prev_rev_itr != revisions.end());
      CatalogPathMap::iterator prev_clg_itr =
        prev_rev_itr->second.find(root_path);
      if (prev_clg_itr != prev_rev_itr->second.end()) {
        previous_catalog = prev_clg_itr->second;
      }
    }

    // produce the new catalog with references to it's predecessor and parent
    const bool is_root = (parent == NULL);
    MockCatalog *catalog = new MockCatalog(root_path,
                                           effective_clg_hash,
                                           dice_.Next(10000),
                                           revision,
                                           is_root,
                                           parent,
                                           previous_catalog);

    // register the new catalog in the data structures
    MockCatalog::RegisterCatalog(catalog);
    catalogs[root_path] = catalog;
    return catalog;
  }

 private:
  Prng dice_;
};


//
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//


TEST_F(T_CatalogTraversal, Initialize) {
  CatalogTraversalParams params;
  MockedCatalogTraversal traverse(params);
}


//
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//


CatalogIdentifiers SimpleTraversal_visited_catalogs;
void SimpleTraversalCallback(const MockedCatalogTraversal::CallbackData &data) {
  SimpleTraversal_visited_catalogs.push_back(
    std::make_pair(data.catalog->GetRevision(), data.catalog->path().ToString()));
}

TEST_F(T_CatalogTraversal, SimpleTraversal) {
  SimpleTraversal_visited_catalogs.clear();
  EXPECT_EQ (0u, SimpleTraversal_visited_catalogs.size());

  CatalogTraversalParams params;
  MockedCatalogTraversal traverse(params);
  traverse.RegisterListener(&SimpleTraversalCallback);
  traverse.Traverse();

  CatalogIdentifiers catalogs;
  catalogs.push_back(std::make_pair(6, ""));
  catalogs.push_back(std::make_pair(5, "/00/13"));
  catalogs.push_back(std::make_pair(5, "/00/13/29"));
  catalogs.push_back(std::make_pair(5, "/00/13/28"));
  catalogs.push_back(std::make_pair(4, "/00/12"));
  catalogs.push_back(std::make_pair(4, "/00/12/27"));
  catalogs.push_back(std::make_pair(4, "/00/12/26"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/38"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/37"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/36"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/35"));
  catalogs.push_back(std::make_pair(4, "/00/12/25"));
  catalogs.push_back(std::make_pair(4, "/00/11"));
  catalogs.push_back(std::make_pair(4, "/00/11/24"));
  catalogs.push_back(std::make_pair(4, "/00/11/23"));
  catalogs.push_back(std::make_pair(4, "/00/11/22"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/43"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/42"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/41"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/33"));

  CheckVisitedCatalogs(catalogs, SimpleTraversal_visited_catalogs);
}


//
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//


std::vector<MockCatalog*> SimpleTraversalNoCloseCallback_visited_catalogs;
void SimpleTraversalNoCloseCallback(
                             const MockedCatalogTraversal::CallbackData &data) {
  SimpleTraversalNoCloseCallback_visited_catalogs.push_back(
    const_cast<MockCatalog*>(data.catalog));
}

TEST_F(T_CatalogTraversal, SimpleTraversalNoClose) {
  SimpleTraversalNoCloseCallback_visited_catalogs.clear();
  EXPECT_EQ (0u, SimpleTraversalNoCloseCallback_visited_catalogs.size());

  CatalogTraversalParams params;
  params.no_close = true;
  MockedCatalogTraversal traverse(params);
  traverse.RegisterListener(&SimpleTraversalNoCloseCallback);
  traverse.Traverse();

  EXPECT_EQ (21u + intial_catalog_instances, MockCatalog::instances);

  std::vector<MockCatalog*>::const_iterator i, iend;
  for (i    = SimpleTraversalNoCloseCallback_visited_catalogs.begin(),
       iend = SimpleTraversalNoCloseCallback_visited_catalogs.end();
       i != iend; ++i) {
    delete *i;
  }
  SimpleTraversalNoCloseCallback_visited_catalogs.clear();
}



//
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//


CatalogIdentifiers ZeroLevelHistoryTraversal_visited_catalogs;
void ZeroLevelHistoryTraversalCallback(
                             const MockedCatalogTraversal::CallbackData &data) {
  ZeroLevelHistoryTraversal_visited_catalogs.push_back(
    std::make_pair(data.catalog->GetRevision(), data.catalog->path().ToString()));
}

TEST_F(T_CatalogTraversal, ZeroLevelHistoryTraversal) {
  ZeroLevelHistoryTraversal_visited_catalogs.clear();
  EXPECT_EQ (0u, ZeroLevelHistoryTraversal_visited_catalogs.size());

  CatalogTraversalParams params;
  params.history = 0;
  MockedCatalogTraversal traverse(params);
  traverse.RegisterListener(&ZeroLevelHistoryTraversalCallback);
  traverse.Traverse();

  CatalogIdentifiers catalogs;
  catalogs.push_back(std::make_pair(6, ""));
  catalogs.push_back(std::make_pair(5, "/00/13"));
  catalogs.push_back(std::make_pair(5, "/00/13/29"));
  catalogs.push_back(std::make_pair(5, "/00/13/28"));
  catalogs.push_back(std::make_pair(4, "/00/12"));
  catalogs.push_back(std::make_pair(4, "/00/12/27"));
  catalogs.push_back(std::make_pair(4, "/00/12/26"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/38"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/37"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/36"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/35"));
  catalogs.push_back(std::make_pair(4, "/00/12/25"));
  catalogs.push_back(std::make_pair(4, "/00/11"));
  catalogs.push_back(std::make_pair(4, "/00/11/24"));
  catalogs.push_back(std::make_pair(4, "/00/11/23"));
  catalogs.push_back(std::make_pair(4, "/00/11/22"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/43"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/42"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/41"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/33"));

  CheckVisitedCatalogs(catalogs, ZeroLevelHistoryTraversal_visited_catalogs);
}


//
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//


CatalogIdentifiers FirstLevelHistoryTraversal_visited_catalogs;
void FirstLevelHistoryTraversalCallback(
                             const MockedCatalogTraversal::CallbackData &data) {
  FirstLevelHistoryTraversal_visited_catalogs.push_back(
    std::make_pair(data.catalog->GetRevision(), data.catalog->path().ToString()));
}

TEST_F(T_CatalogTraversal, FirstLevelHistoryTraversal) {
  FirstLevelHistoryTraversal_visited_catalogs.clear();
  EXPECT_EQ (0u, FirstLevelHistoryTraversal_visited_catalogs.size());

  CatalogTraversalParams params;
  params.history = 1;
  MockedCatalogTraversal traverse(params);
  traverse.RegisterListener(&FirstLevelHistoryTraversalCallback);
  traverse.Traverse();

  CatalogIdentifiers catalogs;
  catalogs.push_back(std::make_pair(6, ""));
  catalogs.push_back(std::make_pair(5, "/00/13"));
  catalogs.push_back(std::make_pair(5, "/00/13/29"));
  catalogs.push_back(std::make_pair(5, "/00/13/28"));
  catalogs.push_back(std::make_pair(4, "/00/12"));
  catalogs.push_back(std::make_pair(4, "/00/12/27"));
  catalogs.push_back(std::make_pair(4, "/00/12/26"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/38"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/37"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/36"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/35"));
  catalogs.push_back(std::make_pair(4, "/00/12/25"));
  catalogs.push_back(std::make_pair(4, "/00/11"));
  catalogs.push_back(std::make_pair(4, "/00/11/24"));
  catalogs.push_back(std::make_pair(4, "/00/11/23"));
  catalogs.push_back(std::make_pair(4, "/00/11/22"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/43"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/42"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/41"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/33"));
  catalogs.push_back(std::make_pair(5, ""));
  catalogs.push_back(std::make_pair(4, "/00/12"));
  catalogs.push_back(std::make_pair(4, "/00/12/27"));
  catalogs.push_back(std::make_pair(4, "/00/12/26"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/38"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/37"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/36"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/35"));
  catalogs.push_back(std::make_pair(4, "/00/12/25"));
  catalogs.push_back(std::make_pair(4, "/00/11"));
  catalogs.push_back(std::make_pair(4, "/00/11/24"));
  catalogs.push_back(std::make_pair(4, "/00/11/23"));
  catalogs.push_back(std::make_pair(4, "/00/11/22"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/43"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/42"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/41"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/33"));
  catalogs.push_back(std::make_pair(2, "/00/10"));
  catalogs.push_back(std::make_pair(2, "/00/10/21"));
  catalogs.push_back(std::make_pair(2, "/00/10/20"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/32"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/31"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/30"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/30/40"));
  catalogs.push_back(std::make_pair(5, "/00/13"));
  catalogs.push_back(std::make_pair(5, "/00/13/29"));
  catalogs.push_back(std::make_pair(5, "/00/13/28"));

  CheckVisitedCatalogs(catalogs, FirstLevelHistoryTraversal_visited_catalogs);
}


//
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//


std::vector<MockCatalog*> FirstLevelHistoryTraversalNoClose_visited_catalogs;
void FirstLevelHistoryTraversalNoCloseCallback(
                             const MockedCatalogTraversal::CallbackData &data) {
  FirstLevelHistoryTraversalNoClose_visited_catalogs.push_back(
                                        const_cast<MockCatalog*>(data.catalog));
}

TEST_F(T_CatalogTraversal, FirstLevelHistoryTraversalNoClose) {
  FirstLevelHistoryTraversalNoClose_visited_catalogs.clear();
  EXPECT_EQ (0u, FirstLevelHistoryTraversalNoClose_visited_catalogs.size());

  CatalogTraversalParams params;
  params.history  = 1;
  params.no_close = true;
  MockedCatalogTraversal traverse(params);
  traverse.RegisterListener(&FirstLevelHistoryTraversalNoCloseCallback);
  traverse.Traverse();

  EXPECT_EQ (49u + intial_catalog_instances, MockCatalog::instances);

  std::vector<MockCatalog*>::const_iterator i, iend;
  for (i    = FirstLevelHistoryTraversalNoClose_visited_catalogs.begin(),
       iend = FirstLevelHistoryTraversalNoClose_visited_catalogs.end();
       i != iend; ++i) {
    delete *i;
  }
  FirstLevelHistoryTraversalNoClose_visited_catalogs.clear();
}


//
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//


CatalogIdentifiers SecondLevelHistoryTraversal_visited_catalogs;
void SecondLevelHistoryTraversalCallback(
                             const MockedCatalogTraversal::CallbackData &data) {
  SecondLevelHistoryTraversal_visited_catalogs.push_back(
    std::make_pair(data.catalog->GetRevision(), data.catalog->path().ToString()));
}

TEST_F(T_CatalogTraversal, SecondLevelHistoryTraversal) {
  SecondLevelHistoryTraversal_visited_catalogs.clear();
  EXPECT_EQ (0u, SecondLevelHistoryTraversal_visited_catalogs.size());

  CatalogTraversalParams params;
  params.history = 2;
  MockedCatalogTraversal traverse(params);
  traverse.RegisterListener(&SecondLevelHistoryTraversalCallback);
  traverse.Traverse();

  CatalogIdentifiers catalogs;
  catalogs.push_back(std::make_pair(6, ""));
  catalogs.push_back(std::make_pair(5, "/00/13"));
  catalogs.push_back(std::make_pair(5, "/00/13/29"));
  catalogs.push_back(std::make_pair(5, "/00/13/28"));
  catalogs.push_back(std::make_pair(4, "/00/12"));
  catalogs.push_back(std::make_pair(4, "/00/12/27"));
  catalogs.push_back(std::make_pair(4, "/00/12/26"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/38"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/37"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/36"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/35"));
  catalogs.push_back(std::make_pair(4, "/00/12/25"));
  catalogs.push_back(std::make_pair(4, "/00/11"));
  catalogs.push_back(std::make_pair(4, "/00/11/24"));
  catalogs.push_back(std::make_pair(4, "/00/11/23"));
  catalogs.push_back(std::make_pair(4, "/00/11/22"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/43"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/42"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/41"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/33"));
  catalogs.push_back(std::make_pair(5, ""));
  catalogs.push_back(std::make_pair(4, "/00/12"));
  catalogs.push_back(std::make_pair(4, "/00/12/27"));
  catalogs.push_back(std::make_pair(4, "/00/12/26"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/38"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/37"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/36"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/35"));
  catalogs.push_back(std::make_pair(4, "/00/12/25"));
  catalogs.push_back(std::make_pair(4, "/00/11"));
  catalogs.push_back(std::make_pair(4, "/00/11/24"));
  catalogs.push_back(std::make_pair(4, "/00/11/23"));
  catalogs.push_back(std::make_pair(4, "/00/11/22"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/43"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/42"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/41"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/33"));
  catalogs.push_back(std::make_pair(2, "/00/10"));
  catalogs.push_back(std::make_pair(2, "/00/10/21"));
  catalogs.push_back(std::make_pair(2, "/00/10/20"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/32"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/31"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/30"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/30/40"));
  catalogs.push_back(std::make_pair(5, "/00/13"));
  catalogs.push_back(std::make_pair(5, "/00/13/29"));
  catalogs.push_back(std::make_pair(5, "/00/13/28"));
  catalogs.push_back(std::make_pair(4, ""));
  catalogs.push_back(std::make_pair(4, "/00/12"));
  catalogs.push_back(std::make_pair(4, "/00/12/27"));
  catalogs.push_back(std::make_pair(4, "/00/12/26"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/38"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/37"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/36"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/35"));
  catalogs.push_back(std::make_pair(4, "/00/12/25"));
  catalogs.push_back(std::make_pair(4, "/00/11"));
  catalogs.push_back(std::make_pair(4, "/00/11/24"));
  catalogs.push_back(std::make_pair(4, "/00/11/23"));
  catalogs.push_back(std::make_pair(4, "/00/11/22"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/43"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/42"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/41"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/33"));
  catalogs.push_back(std::make_pair(2, "/00/10"));
  catalogs.push_back(std::make_pair(2, "/00/10/21"));
  catalogs.push_back(std::make_pair(2, "/00/10/20"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/32"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/31"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/30"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/30/40"));

  CheckVisitedCatalogs(catalogs, SecondLevelHistoryTraversal_visited_catalogs);
}


//
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//


CatalogIdentifiers FullHistoryTraversal_visited_catalogs;
void FullHistoryTraversalCallback(
                             const MockedCatalogTraversal::CallbackData &data) {
  FullHistoryTraversal_visited_catalogs.push_back(
    std::make_pair(data.catalog->GetRevision(), data.catalog->path().ToString()));
}

TEST_F(T_CatalogTraversal, FullHistoryTraversal) {
  FullHistoryTraversal_visited_catalogs.clear();
  EXPECT_EQ (0u, FullHistoryTraversal_visited_catalogs.size());

  CatalogTraversalParams params;
  params.history = CatalogTraversalParams::kFullHistory;
  MockedCatalogTraversal traverse(params);
  traverse.RegisterListener(&FullHistoryTraversalCallback);
  traverse.Traverse();

  CatalogIdentifiers catalogs;
  catalogs.push_back(std::make_pair(6, ""));
  catalogs.push_back(std::make_pair(5, "/00/13"));
  catalogs.push_back(std::make_pair(5, "/00/13/29"));
  catalogs.push_back(std::make_pair(5, "/00/13/28"));
  catalogs.push_back(std::make_pair(4, "/00/12"));
  catalogs.push_back(std::make_pair(4, "/00/12/27"));
  catalogs.push_back(std::make_pair(4, "/00/12/26"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/38"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/37"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/36"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/35"));
  catalogs.push_back(std::make_pair(4, "/00/12/25"));
  catalogs.push_back(std::make_pair(4, "/00/11"));
  catalogs.push_back(std::make_pair(4, "/00/11/24"));
  catalogs.push_back(std::make_pair(4, "/00/11/23"));
  catalogs.push_back(std::make_pair(4, "/00/11/22"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/43"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/42"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/41"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/33"));
  catalogs.push_back(std::make_pair(5, ""));
  catalogs.push_back(std::make_pair(4, "/00/12"));
  catalogs.push_back(std::make_pair(4, "/00/12/27"));
  catalogs.push_back(std::make_pair(4, "/00/12/26"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/38"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/37"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/36"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/35"));
  catalogs.push_back(std::make_pair(4, "/00/12/25"));
  catalogs.push_back(std::make_pair(4, "/00/11"));
  catalogs.push_back(std::make_pair(4, "/00/11/24"));
  catalogs.push_back(std::make_pair(4, "/00/11/23"));
  catalogs.push_back(std::make_pair(4, "/00/11/22"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/43"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/42"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/41"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/33"));
  catalogs.push_back(std::make_pair(2, "/00/10"));
  catalogs.push_back(std::make_pair(2, "/00/10/21"));
  catalogs.push_back(std::make_pair(2, "/00/10/20"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/32"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/31"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/30"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/30/40"));
  catalogs.push_back(std::make_pair(5, "/00/13"));
  catalogs.push_back(std::make_pair(5, "/00/13/29"));
  catalogs.push_back(std::make_pair(5, "/00/13/28"));
  catalogs.push_back(std::make_pair(4, ""));
  catalogs.push_back(std::make_pair(4, "/00/12"));
  catalogs.push_back(std::make_pair(4, "/00/12/27"));
  catalogs.push_back(std::make_pair(4, "/00/12/26"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/38"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/37"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/36"));
  catalogs.push_back(std::make_pair(4, "/00/12/26/35"));
  catalogs.push_back(std::make_pair(4, "/00/12/25"));
  catalogs.push_back(std::make_pair(4, "/00/11"));
  catalogs.push_back(std::make_pair(4, "/00/11/24"));
  catalogs.push_back(std::make_pair(4, "/00/11/23"));
  catalogs.push_back(std::make_pair(4, "/00/11/22"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/43"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/42"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/34/41"));
  catalogs.push_back(std::make_pair(4, "/00/11/22/33"));
  catalogs.push_back(std::make_pair(2, "/00/10"));
  catalogs.push_back(std::make_pair(2, "/00/10/21"));
  catalogs.push_back(std::make_pair(2, "/00/10/20"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/32"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/31"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/30"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/30/40"));
  catalogs.push_back(std::make_pair(3, ""));
  catalogs.push_back(std::make_pair(3, "/00/11"));
  catalogs.push_back(std::make_pair(3, "/00/11/24"));
  catalogs.push_back(std::make_pair(3, "/00/11/23"));
  catalogs.push_back(std::make_pair(3, "/00/11/22"));
  catalogs.push_back(std::make_pair(3, "/00/11/22/34"));
  catalogs.push_back(std::make_pair(3, "/00/11/22/34/43"));
  catalogs.push_back(std::make_pair(3, "/00/11/22/34/42"));
  catalogs.push_back(std::make_pair(3, "/00/11/22/34/41"));
  catalogs.push_back(std::make_pair(3, "/00/11/22/33"));
  catalogs.push_back(std::make_pair(2, "/00/10"));
  catalogs.push_back(std::make_pair(2, "/00/10/21"));
  catalogs.push_back(std::make_pair(2, "/00/10/20"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/32"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/31"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/30"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/30/40"));
  catalogs.push_back(std::make_pair(2, ""));
  catalogs.push_back(std::make_pair(2, "/00/10"));
  catalogs.push_back(std::make_pair(2, "/00/10/21"));
  catalogs.push_back(std::make_pair(2, "/00/10/20"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/32"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/31"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/30"));
  catalogs.push_back(std::make_pair(2, "/00/10/20/30/40"));
  catalogs.push_back(std::make_pair(1, ""));

  CheckVisitedCatalogs(catalogs, FullHistoryTraversal_visited_catalogs);
}
