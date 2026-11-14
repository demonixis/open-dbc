#pragma once

// A form is a list of rows, each one value with its label: the SwiftUI Studio draws the Inspector
// and the Level Settings from these, one row below the other, instead of from ImGui widgets.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "core/i18n.hpp"

namespace dbc::studio::swiftui
{
    enum class FieldKind
    {
        Heading,
        Hint,
        Warning,
        Text,
        Number,
        Toggle,
        Color,
        Choice,
        Asset,
        Button,
    };

    struct Field
    {
        FieldKind Kind = FieldKind::Hint;
        std::string Label;
        std::string Tooltip;
        bool Enabled = true;
        bool Optional = false;
        bool Set = true;
        double Number = 0.0;
        double Min = -1.0e30;
        double Max = 1.0e30;
        double Step = 1.0;
        bool Integer = false;
        bool Slider = false;
        std::string Text;
        bool Flag = false;
        std::uint32_t Color = 0;
        std::vector<std::string> Choices;
        int Choice = 0;
        int AssetKind = -1;

        // They hold pointers into the document: valid until it changes, so a form is built again
        // before each edit.
        std::function<void(double)> SetNumber;
        std::function<void(const std::string&)> SetText;
        std::function<void(bool)> SetFlag;
        std::function<void(bool)> SetOptional;
        std::function<void()> Press;
    };

    // Labels are English source texts: the builder translates them.
    class FormBuilder
    {
      public:
        std::vector<Field> Fields;
        // Rows added while false are shown disabled.
        bool Enabled = true;

        Field& Add(FieldKind Kind, const std::string& Label)
        {
            Field Value;
            Value.Kind = Kind;
            Value.Label = Label;
            Value.Enabled = Enabled;
            Fields.push_back(std::move(Value));
            return Fields.back();
        }

        void Heading(const char* Label)
        {
            Add(FieldKind::Heading, Tr(Label));
        }

        void Hint(const std::string& Text)
        {
            Add(FieldKind::Hint, Text);
        }

        void Warning(const std::string& Text)
        {
            Add(FieldKind::Warning, Text);
        }

        Field& Float(const std::string& Label, float& Value, double Step = 1.0, double Min = -1.0e30,
                     double Max = 1.0e30)
        {
            Field& Row = Add(FieldKind::Number, Label);
            Row.Number = Value;
            Row.Step = Step;
            Row.Min = Min;
            Row.Max = Max;
            float* Target = &Value;
            Row.SetNumber = [Target, Min, Max](double Number)
            { *Target = static_cast<float>(std::min(std::max(Number, Min), Max)); };
            return Row;
        }

        Field& Int(const std::string& Label, int& Value, int Min, int Max, bool Slider = false)
        {
            Field& Row = Add(FieldKind::Number, Label);
            Row.Number = Value;
            Row.Integer = true;
            Row.Slider = Slider;
            Row.Min = Min;
            Row.Max = Max;
            int* Target = &Value;
            Row.SetNumber = [Target, Min, Max](double Number)
            { *Target = static_cast<int>(std::min<double>(std::max<double>(Number, Min), Max)); };
            return Row;
        }

        Field& Toggle(const std::string& Label, bool& Value)
        {
            Field& Row = Add(FieldKind::Toggle, Label);
            Row.Flag = Value;
            bool* Target = &Value;
            Row.SetFlag = [Target](bool Flag) { *Target = Flag; };
            return Row;
        }

        Field& Toggle(const std::string& Label, bool Value, std::function<void(bool)> Set)
        {
            Field& Row = Add(FieldKind::Toggle, Label);
            Row.Flag = Value;
            Row.SetFlag = std::move(Set);
            return Row;
        }

        Field& Text(const std::string& Label, std::string& Value)
        {
            Field& Row = Add(FieldKind::Text, Label);
            Row.Text = Value;
            std::string* Target = &Value;
            Row.SetText = [Target](const std::string& Text) { *Target = Text; };
            return Row;
        }

        Field& Color(const std::string& Label, std::uint32_t& Value)
        {
            Field& Row = Add(FieldKind::Color, Label);
            Row.Color = Value;
            std::uint32_t* Target = &Value;
            Row.SetNumber = [Target](double Number) { *Target = static_cast<std::uint32_t>(Number) & 0xFFFFFFu; };
            return Row;
        }

        Field& Choice(const std::string& Label, std::vector<std::string> Choices, int Current,
                      std::function<void(int)> Set)
        {
            Field& Row = Add(FieldKind::Choice, Label);
            Row.Choices = std::move(Choices);
            Row.Choice = Current;
            Row.SetNumber = [Set = std::move(Set)](double Number) { Set(static_cast<int>(Number)); };
            return Row;
        }

        // `Kind` is a core::MediaKind, or -1 for any file.
        Field& Asset(const std::string& Label, std::string& Value, int Kind)
        {
            Field& Row = Add(FieldKind::Asset, Label);
            Row.Text = Value;
            Row.AssetKind = Kind;
            std::string* Target = &Value;
            Row.SetText = [Target](const std::string& Text) { *Target = Text; };
            return Row;
        }

        Field& Button(const std::string& Label, std::function<void()> Press)
        {
            Field& Row = Add(FieldKind::Button, Label);
            Row.Press = std::move(Press);
            return Row;
        }

        // One row per axis: a row holding three numbers is too long to read or to touch.
        void Vector(const char* Label, float* Values, int Count, double Step = 1.0, double Min = -1.0e30,
                    double Max = 1.0e30, const char* const* Names = nullptr)
        {
            static constexpr const char* KAxes[] = {"X", "Y", "Z"};
            for (int Index = 0; Index < Count; ++Index)
                Float(std::string(Tr(Label)) + " " + (Names != nullptr ? Tr(Names[Index]) : KAxes[Index]),
                      Values[Index], Step, Min, Max);
        }

        // A value the level may leave to the engine. `Edit` adds the rows of `Value`, or of a copy
        // of `Default` shown disabled.
        template <typename T, typename EditRows>
        void Optional(std::optional<T>& Value, const T& Default, T& Shown, EditRows&& Edit)
        {
            const std::size_t First = Fields.size();
            const bool WasEnabled = Enabled;
            if (Value)
            {
                Edit(*Value);
            }
            else
            {
                Shown = Default;
                Edit(Shown);
            }
            Enabled = WasEnabled;
            std::optional<T>* Target = &Value;
            for (std::size_t Index = First; Index < Fields.size(); ++Index)
            {
                Field& Row = Fields[Index];
                // The first row carries the switch; the others follow it.
                Row.Optional = Index == First;
                Row.Set = Value.has_value();
                if (!Value)
                {
                    Row.SetNumber = nullptr;
                    Row.SetText = nullptr;
                    Row.SetFlag = nullptr;
                }
                Row.SetOptional = [Target, Default](bool Set)
                { *Target = Set ? std::optional<T>(Default) : std::nullopt; };
            }
        }

        void OptionalFloat(const char* Label, std::optional<float>& Value, float Default, double Step, double Min,
                           double Max)
        {
            Optional(Value, Default, ShownFloat_, [&](float& Number) { Float(Tr(Label), Number, Step, Min, Max); });
        }

        void OptionalInt(const char* Label, std::optional<int>& Value, int Default, int Min, int Max,
                         bool Slider = false)
        {
            Optional(Value, Default, ShownInt_, [&](int& Number) { Int(Tr(Label), Number, Min, Max, Slider); });
        }

        void OptionalColor(const char* Label, std::optional<std::uint32_t>& Value, std::uint32_t Default)
        {
            Optional(Value, Default, ShownColor_, [&](std::uint32_t& Number) { Color(Tr(Label), Number); });
        }

      private:
        // What an unset optional shows: storage for the rows' values while they are built.
        float ShownFloat_ = 0.0f;
        int ShownInt_ = 0;
        std::uint32_t ShownColor_ = 0;
    };
} // namespace dbc::studio::swiftui
