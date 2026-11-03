#pragma once

#include <string>

struct aiScene;

namespace dbc
{
    struct AssimpImportResult;
    struct Object3D;
    class RuntimeCompatibilityPolicy;

    // Shared CPU import for LOAD OBJECT and Studio inspection, retaining the runtime's hierarchy choice.
    [[nodiscard]] AssimpImportResult ImportObjectModel(const std::string& Path, std::string& Error);

    // Populate a fresh object's limb map. The imported scene must outlive the object.
    void BuildImportedLimbMap(const aiScene* Scene, Object3D& Object, const RuntimeCompatibilityPolicy& Policy);
} // namespace dbc
