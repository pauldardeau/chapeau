#ifndef CHAPEAU_TESTS_SCOPED_TEMP_DIR_H
#define CHAPEAU_TESTS_SCOPED_TEMP_DIR_H

#include <chrono>
#include <filesystem>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace chapeau_tests {

/**
 * A directory created for one test and removed again when the object goes
 * out of scope.
 *
 * The directory is created under std::filesystem::temp_directory_path()
 * (so TMPDIR/TMP/TEMP are honoured) with a name that cannot already exist:
 * creation is exclusive and is retried with a new random name on a clash.
 * Only that one directory is ever removed; nothing else in the temporary
 * directory is touched, and no fixed path such as /tmp is involved.
 */
class ScopedTempDir {
public:
   explicit ScopedTempDir(const std::string& prefix = "chapeau-test") {
      std::error_code ec;
      const std::filesystem::path base =
         std::filesystem::temp_directory_path(ec);
      if (ec) {
         throw std::runtime_error("no temporary directory: " + ec.message());
      }

      std::random_device rd;
      for (int attempt = 0; attempt < 100; ++attempt) {
         std::ostringstream name;
         name << prefix << '-'
              << std::chrono::steady_clock::now().time_since_epoch().count()
              << '-' << std::hex << rd() << rd();
         const std::filesystem::path candidate = base / name.str();

         // false (and no error) when the name already exists: try another
         if (std::filesystem::create_directory(candidate, ec) && !ec) {
            m_path = candidate;
            return;
         }
      }
      throw std::runtime_error("could not create a unique temporary directory");
   }

   ~ScopedTempDir() {
      remove();
   }

   const std::filesystem::path& path() const {
      return m_path;
   }

   std::string file(const std::string& name) const {
      return (m_path / name).string();
   }

   /**
    * Removes the directory and everything below it.
    * @return true if the directory no longer exists
    */
   bool remove() {
      if (m_path.empty()) {
         return true;
      }
      std::error_code ec;
      std::filesystem::remove_all(m_path, ec);
      const bool gone = !std::filesystem::exists(m_path, ec);
      if (gone) {
         m_path.clear();
      }
      return gone;
   }

private:
   std::filesystem::path m_path;

   ScopedTempDir(const ScopedTempDir&) = delete;
   ScopedTempDir& operator=(const ScopedTempDir&) = delete;
};

}

#endif
