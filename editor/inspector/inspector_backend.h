#pragma once

#include <spot.h>

#include <imgui.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Spot::Editor {

class EditorProject;
class EulerCache;

// Draws a property row for each field that a component's Serialize() visits, inside a Widgets::BeginProperties()
// table, and changes the fields as their widgets are edited.
//
// Fields are known by JSON pointers into the component's saved form, such as "/data/color/0" for the red of its
// color, so the editor can tell which values differ between selected entities and copy an edit to all of them.
class InspectorBackend final : public ArchiveBackend {
public:
    // Where a field's widgets were drawn, in screen coordinates.
    struct FieldRect {
        std::string pointer;
        ImVec2 min;
        ImVec2 max;
    };

    struct Options {
        // Lists entities to pick; may be null.
        Scene* scene = nullptr;
        // Lists assets to pick and loads them; may be null.
        EditorProject* project = nullptr;
        EulerCache* eulers = nullptr;
        // Pointers to the values that differ between the selected entities, which show as mixed.
        std::span<const std::string> mixed = {};
        // The pointer the fields are under.
        std::string root = "/data";
    };

    explicit InspectorBackend(Options options);

    // Whether a field changed while drawing.
    bool WasChanged() const { return !m_changed.empty(); }
    // Pointers to the values that changed: a single axis of a vector, or a whole field.
    const std::vector<std::string>& GetChanged() const { return m_changed; }
    // How many rows were drawn, or would have been in collapsed objects; 0 for components without fields.
    std::size_t GetFieldCount() const { return m_fieldCount; }
    const std::vector<FieldRect>& GetFieldRects() const { return m_rects; }

    bool IsLoading() const override { return true; }

    void Field(std::string_view name, bool& value, const FieldOptions& options) override;
    void Field(std::string_view name, int& value, const FieldOptions& options) override;
    void Field(std::string_view name, std::uint32_t& value, const FieldOptions& options) override;
    void Field(std::string_view name, std::int64_t& value, const FieldOptions& options) override;
    void Field(std::string_view name, std::uint64_t& value, const FieldOptions& options) override;
    void Field(std::string_view name, float& value, const FieldOptions& options) override;
    void Field(std::string_view name, double& value, const FieldOptions& options) override;
    void Field(std::string_view name, std::string& value, const FieldOptions& options) override;
    void Field(std::string_view name, Vector2& value, const FieldOptions& options) override;
    void Field(std::string_view name, Vector3& value, const FieldOptions& options) override;
    void Field(std::string_view name, Vector4& value, const FieldOptions& options) override;
    void Field(std::string_view name, Quaternion& value, const FieldOptions& options) override;
    void Field(std::string_view name, Color& value, const FieldOptions& options) override;
    void Field(std::string_view name, EntityId& value, const FieldOptions& options) override;
    void Field(std::string_view name, AssetId& value, std::optional<AssetType> type,
               const FieldOptions& options) override;
    void EnumField(std::string_view name, std::int64_t& value, const FieldOptions& options) override;
    void CurveField(std::string_view name, Curve& value, const FieldOptions& options) override;
    void GradientField(std::string_view name, Gradient& value, const FieldOptions& options) override;
    bool BeginObject(std::string_view name, const FieldOptions& options) override;
    void EndObject() override;
    bool BeginArray(std::string_view name, std::size_t& size, const FieldOptions& options) override;
    void BeginElement(std::size_t index) override;
    void EndElement() override;
    void EndArray() override;
    AssetManager* GetAssetManager() const override;

private:
    // An object or array being drawn. Its fields are skipped while it, or one around it, is collapsed.
    struct Container {
        std::string path;
        bool open = false;
    };

    // Starts the row of a field, with its label, unless it is in a collapsed object. Gives the field's pointer.
    bool BeginRow(std::string_view name, const FieldOptions& options, std::string& pointer);
    void EndRow();
    std::string GetPointer(std::string_view name) const;
    std::string GetLabel(std::string_view name) const;
    bool IsMixed(std::string_view pointer) const;
    // A bit for each of the vector's elements that is mixed.
    unsigned GetMixedMask(std::string_view pointer, int count) const;
    void MarkChanged(std::string pointer);
    bool BeginContainer(std::string_view name, const FieldOptions& options, std::size_t* size);
    void EndContainer();
    template <typename T>
    void Number(std::string_view name, T& value, const FieldOptions& options);
    template <std::size_t N>
    void Floats(std::string_view name, float* values, const FieldOptions& options);

    Options m_options;
    std::string m_path;
    std::vector<Container> m_containers;
    std::vector<std::string> m_elementPaths;
    int m_collapsed = 0;
    std::optional<std::size_t> m_element;
    std::vector<std::string> m_changed;
    std::size_t m_fieldCount = 0;
    std::string m_rowPointer;
    std::vector<FieldRect> m_rects;
};

} // namespace Spot::Editor
