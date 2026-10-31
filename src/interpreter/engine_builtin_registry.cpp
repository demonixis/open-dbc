#include "engine_builtin_registry.hpp"
#include "basic_path.hpp"
#include "dba_matrix4.hpp"
#include "dba_vecmath.hpp"
#include "engine/systems/audio_system.hpp"
#include "engine/systems/bsp_world.hpp"
#include "engine/systems/camera_system.hpp"
#include "engine/systems/core_system.hpp"
#include "engine/systems/debug_system.hpp"
#include "engine/systems/dll_system.hpp"
#include "engine/systems/engine_kernel.hpp"
#include "engine/systems/file_io_system.hpp"
#include "engine/systems/graphics_device_system.hpp"
#include "engine/systems/image_store.hpp"
#include "engine/systems/input_system.hpp"
#include "engine/systems/light_manager.hpp"
#include "engine/systems/log.hpp"
#include "engine/systems/matrix_manager.hpp"
#include "engine/systems/memblock_system.hpp"
#include "engine/systems/network_system.hpp"
#include "engine/systems/object_manager.hpp"
#include "engine/systems/physics_system.hpp"
#include "engine/systems/renderer_system.hpp"
#include "engine/systems/video_system.hpp"
#include "engine/systems/vr_system.hpp"
#include "engine_builtin_api/all.hpp"
#include "language/language_catalogue.hpp"
#include "language/numeric_conversion.hpp"
#include "path_utf8.hpp"
#include "registry_file.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifdef _WIN32
#undef LoadImage
#undef LoadBitmap
#undef GetObject
#undef DrawText
#endif

namespace dbc::interpreter::detail
{
    double BuiltinValue::AsNumber() const
    {
        if (!IsString)
            return Number;
        if (Text.empty())
            return 0.0;
        double Result = 0.0;
        std::sscanf(Text.c_str(), "%lf", &Result);
        return Result;
    }

    bool BuiltinValue::AsFloat(float& Output, std::string* Error) const
    {
        const double Value = AsNumber();
        if (!std::isfinite(Value) || Value < -std::numeric_limits<float>::max() ||
            Value > std::numeric_limits<float>::max())
        {
            if (Error)
                *Error = "Numeric value is out of floating-point range";
            return false;
        }
        Output = static_cast<float>(Value);
        return true;
    }

    bool BuiltinValue::AsInt(int& Output, std::string* Error) const
    {
        std::string ConversionError;
        if (!language::ConvertInteger(AsNumber(), Output, &ConversionError))
        {
            if (Error)
                *Error = std::move(ConversionError);
            return false;
        }
        return true;
    }

    bool BuiltinValue::AsDword(std::uint32_t& Output, std::string* Error) const
    {
        std::int64_t Value = 0;
        std::string ConversionError;
        if (!language::ConvertInteger(AsNumber(), Value, &ConversionError) ||
            Value < std::numeric_limits<std::int32_t>::min() || Value > std::numeric_limits<std::uint32_t>::max())
        {
            if (Error)
                *Error = ConversionError.empty() ? "Numeric value is out of DWORD range" : std::move(ConversionError);
            return false;
        }
        Output = static_cast<std::uint32_t>(Value);
        return true;
    }

    bool BuiltinValue::AsBool() const
    {
        return IsString ? !Text.empty() : Number != 0.0;
    }

    std::string BuiltinValue::AsString() const
    {
        if (IsString)
            return Text;
        char Buffer[32];
        const int Length = std::snprintf(Buffer, sizeof(Buffer), "%.12g", Number);
        if (Length <= 0 || Length >= static_cast<int>(sizeof(Buffer)))
            return "0";
        return std::string(Buffer, static_cast<std::size_t>(Length));
    }

    namespace
    {
        namespace fs = std::filesystem;
        constexpr int KMaximumMathValue = 22000000;

        template <typename Map>
        typename Map::mapped_type* RequireVector(Map& Vectors, int Id, const char* Command,
                                                 std::string* Error = nullptr)
        {
            if (Id < 1 || Id > KMaximumMathValue)
            {
                if (Error)
                    *Error = std::string(Command) + " requires a vector number from 1 to 22000000";
                return nullptr;
            }
            const auto It = Vectors.find(Id);
            if (It == Vectors.end())
            {
                if (Error)
                    *Error = std::string(Command) + " requires existing vector " + std::to_string(Id);
                return nullptr;
            }
            return &It->second;
        }

        template <typename Map>
        bool StoreVector(Map& Vectors, int Id, typename Map::mapped_type Value, const char* Command,
                         std::string* Error = nullptr)
        {
            auto* CheckedMathValue1 = RequireVector(Vectors, Id, Command, Error);
            if (!CheckedMathValue1)
                return false;

            (*CheckedMathValue1) = std::move(Value);
            return true;
        }

        Mat4* RequireMatrix4(BuiltinContext& Context, int Id, const char* Command, std::string* Error = nullptr)
        {
            if (Id < 1 || Id > KMaximumMathValue)
            {
                if (Error)
                    *Error = std::string(Command) + " requires a matrix number from 1 to 22000000";
                return nullptr;
            }
            const auto It = Context.Matrices4.find(Id);
            if (It == Context.Matrices4.end())
            {
                if (Error)
                    *Error = std::string(Command) + " requires existing matrix " + std::to_string(Id);
                return nullptr;
            }
            return &It->second;
        }

        bool StoreMatrix4(BuiltinContext& Context, int Id, Mat4 Value, const char* Command,
                          std::string* Error = nullptr)
        {
            auto* CheckedMathValue1 = RequireMatrix4(Context, Id, Command, Error);
            if (!CheckedMathValue1)
                return false;

            (*CheckedMathValue1) = std::move(Value);
            return true;
        }

        void ClearMathValue(BuiltinContext& Context, int Id)
        {
            Context.Vectors2.erase(Id);
            Context.Vectors.erase(Id);
            Context.Vectors4.erase(Id);
            Context.Matrices4.erase(Id);
        }

        template <typename Map>
        bool MakeVector(BuiltinContext& Context, Map& Vectors, int Id, const char* Command,
                        std::string* Error = nullptr)
        {
            if (Id < 1 || Id > KMaximumMathValue)
            {
                if (Error)
                    *Error = std::string(Command) + " requires a vector number from 1 to 22000000";
                return false;
            }
            ClearMathValue(Context, Id);
            Vectors[Id] = typename Map::mapped_type{};
            return true;
        }

        bool ReadVrVector(BuiltinContext& Context, const std::vector<BuiltinValue>& Arguments, bool Controller,
                          bool Rotation, std::string* Error)
        {
            int Hand = 0, Id = 0;
            if (Controller && !Arguments[0].AsInt(Hand, Error))
                return false;
            if (!Arguments[Controller ? 1 : 0].AsInt(Id, Error))
                return false;
            VrPose Pose;
            if (Controller)
            {
                if (!Context.Kernel->Vr().ControllerPose(Hand, Pose, Error))
                    return false;
            }
            else
                Pose = Context.Kernel->Vr().HeadPose();
            if (!MakeVector(Context, Context.Vectors, Id, "GET VR", Error))
                return false;
            const auto Value = Rotation ? Pose.Rotation : Pose.Position;
            Context.Vectors[Id] = {Value.x, Value.y, Value.z};
            return true;
        }

        bool MakeMatrix4Command(BuiltinContext& Context, int Id, std::string* Error = nullptr)
        {
            if (Id < 1 || Id > KMaximumMathValue)
            {
                if (Error)
                    *Error = "MAKE MATRIX4 requires a matrix number from 1 to 22000000";
                return false;
            }
            ClearMathValue(Context, Id);
            Context.Matrices4[Id] = Mat4{};
            return true;
        }

        bool DeleteMatrix4(BuiltinContext& Context, int Id, std::string* Error = nullptr)
        {
            if (Id < 1 || Id > KMaximumMathValue)
            {
                if (Error)
                    *Error = "DELETE MATRIX4 requires a matrix number from 1 to 22000000";
                return false;
            }
            Context.Matrices4.erase(Id);
            return true;
        }

        bool AddMatrix4(BuiltinContext& Context, const std::vector<BuiltinValue>& Arguments, const char* Command,
                        std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Arguments.size() > 0 && !Arguments[0].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Arguments.size() > 1 && !Arguments[1].AsInt(IntegerArgument1, Error))
                return false;
            int IntegerArgument2 = 0;
            if (Arguments.size() > 2 && !Arguments[2].AsInt(IntegerArgument2, Error))
                return false;
            auto* CheckedMathValue1 = RequireMatrix4(Context, IntegerArgument1, Command, Error);
            if (!CheckedMathValue1)
                return false;
            auto* CheckedMathValue2 = RequireMatrix4(Context, IntegerArgument2, Command, Error);
            if (!CheckedMathValue2)
                return false;

            {
                if (!StoreMatrix4(Context, IntegerArgument0, Mat4Add((*CheckedMathValue1), (*CheckedMathValue2)),
                                  Command, Error))
                    return false;
            }
            return true;
        }

        bool SubtractMatrix4(BuiltinContext& Context, const std::vector<BuiltinValue>& Arguments, const char* Command,
                             std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Arguments.size() > 0 && !Arguments[0].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Arguments.size() > 1 && !Arguments[1].AsInt(IntegerArgument1, Error))
                return false;
            int IntegerArgument2 = 0;
            if (Arguments.size() > 2 && !Arguments[2].AsInt(IntegerArgument2, Error))
                return false;
            auto* CheckedMathValue1 = RequireMatrix4(Context, IntegerArgument1, Command, Error);
            if (!CheckedMathValue1)
                return false;
            auto* CheckedMathValue2 = RequireMatrix4(Context, IntegerArgument2, Command, Error);
            if (!CheckedMathValue2)
                return false;

            {
                if (!StoreMatrix4(Context, IntegerArgument0, Mat4Subtract((*CheckedMathValue1), (*CheckedMathValue2)),
                                  Command, Error))
                    return false;
            }
            return true;
        }

        bool MultiplyMatrix4(BuiltinContext& Context, const std::vector<BuiltinValue>& Arguments,
                             std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Arguments.size() > 0 && !Arguments[0].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Arguments.size() == 3 && !Arguments[1].AsInt(IntegerArgument1, Error))
                return false;
            int IntegerArgument2 = 0;
            if (Arguments.size() > 2 && !Arguments[2].AsInt(IntegerArgument2, Error))
                return false;
            if (Arguments.size() == 3)
            {
                auto* CheckedMathValue1 = RequireMatrix4(Context, IntegerArgument1, "MULTIPLY MATRIX4", Error);
                if (!CheckedMathValue1)
                    return false;
                auto* CheckedMathValue2 = RequireMatrix4(Context, IntegerArgument2, "MULTIPLY MATRIX4", Error);
                if (!CheckedMathValue2)
                    return false;

                {
                    if (!StoreMatrix4(Context, IntegerArgument0,
                                      Mat4Multiply((*CheckedMathValue1), (*CheckedMathValue2)), "MULTIPLY MATRIX4",
                                      Error))
                        return false;
                }
                return true;
            }
            auto* CheckedMathValue3 = RequireMatrix4(Context, IntegerArgument0, "MULTIPLY MATRIX4", Error);
            if (!CheckedMathValue3)
                return false;

            auto& Matrix = (*CheckedMathValue3);
            float FloatArgument1 = 0.0f;
            if (Arguments.size() > 1 && !Arguments[1].AsFloat(FloatArgument1, Error))
                return false;

            Matrix = Mat4Scale(Matrix, FloatArgument1);
            return true;
        }

        bool DivideMatrix4(BuiltinContext& Context, const std::vector<BuiltinValue>& Arguments,
                           std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Arguments.size() > 0 && !Arguments[0].AsInt(IntegerArgument0, Error))
                return false;
            auto* CheckedMathValue1 = RequireMatrix4(Context, IntegerArgument0, "DIVIDE MATRIX4", Error);
            if (!CheckedMathValue1)
                return false;

            auto& Matrix = (*CheckedMathValue1);
            float FloatArgument1 = 0.0f;
            if (Arguments.size() > 1 && !Arguments[1].AsFloat(FloatArgument1, Error))
                return false;

            Matrix = Mat4Scale(Matrix, 1.0f / FloatArgument1);
            return true;
        }

        bool BuildLookAtMatrix4(BuiltinContext& Context, const std::vector<BuiltinValue>& Arguments, bool LeftHanded,
                                const char* Command, std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Arguments.size() > 1 && !Arguments[1].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Arguments.size() > 2 && !Arguments[2].AsInt(IntegerArgument1, Error))
                return false;
            int IntegerArgument2 = 0;
            if (Arguments.size() > 3 && !Arguments[3].AsInt(IntegerArgument2, Error))
                return false;
            int IntegerArgument3 = 0;
            if (Arguments.size() > 0 && !Arguments[0].AsInt(IntegerArgument3, Error))
                return false;
            auto* CheckedMathValue1 = RequireVector(Context.Vectors, IntegerArgument0, Command, Error);
            if (!CheckedMathValue1)
                return false;

            const auto& Eye = (*CheckedMathValue1);
            auto* CheckedMathValue2 = RequireVector(Context.Vectors, IntegerArgument1, Command, Error);
            if (!CheckedMathValue2)
                return false;

            const auto& At = (*CheckedMathValue2);
            auto* CheckedMathValue3 = RequireVector(Context.Vectors, IntegerArgument2, Command, Error);
            if (!CheckedMathValue3)
                return false;

            const auto& Up = (*CheckedMathValue3);
            {
                if (!StoreMatrix4(
                        Context, IntegerArgument3,
                        Mat4LookAt(Eye[0], Eye[1], Eye[2], At[0], At[1], At[2], Up[0], Up[1], Up[2], LeftHanded),
                        Command, Error))
                    return false;
            }
            return true;
        }

        bool BuildRotationAxisMatrix4(BuiltinContext& Context, const std::vector<BuiltinValue>& Arguments,
                                      std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Arguments.size() > 1 && !Arguments[1].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Arguments.size() > 0 && !Arguments[0].AsInt(IntegerArgument1, Error))
                return false;
            auto* CheckedMathValue1 =
                RequireVector(Context.Vectors, IntegerArgument0, "BUILD ROTATION AXIS MATRIX4", Error);
            if (!CheckedMathValue1)
                return false;

            const auto& Axis = (*CheckedMathValue1);
            {
                float FloatArgument1 = 0.0f;
                if (Arguments.size() > 2 && !Arguments[2].AsFloat(FloatArgument1, Error))
                    return false;

                if (!StoreMatrix4(Context, IntegerArgument1,
                                  Mat4RotationAxis(Axis[0], Axis[1], Axis[2], FloatArgument1),
                                  "BUILD ROTATION AXIS MATRIX4", Error))
                    return false;
            }
            return true;
        }

        bool ProjectVector3(BuiltinContext& Context, const std::vector<BuiltinValue>& Arguments,
                            std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Arguments.size() > 0 && !Arguments[0].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Arguments.size() > 1 && !Arguments[1].AsInt(IntegerArgument1, Error))
                return false;
            int IntegerArgument2 = 0;
            if (Arguments.size() > 2 && !Arguments[2].AsInt(IntegerArgument2, Error))
                return false;
            int IntegerArgument3 = 0;
            if (Arguments.size() > 3 && !Arguments[3].AsInt(IntegerArgument3, Error))
                return false;
            int IntegerArgument4 = 0;
            if (Arguments.size() > 4 && !Arguments[4].AsInt(IntegerArgument4, Error))
                return false;
            auto* CheckedMathValue1 = RequireVector(Context.Vectors, IntegerArgument1, "PROJECT VECTOR3", Error);
            if (!CheckedMathValue1)
                return false;
            auto* CheckedMathValue2 = RequireMatrix4(Context, IntegerArgument2, "PROJECT VECTOR3", Error);
            if (!CheckedMathValue2)
                return false;
            auto* CheckedMathValue3 = RequireMatrix4(Context, IntegerArgument3, "PROJECT VECTOR3", Error);
            if (!CheckedMathValue3)
                return false;
            auto* CheckedMathValue4 = RequireMatrix4(Context, IntegerArgument4, "PROJECT VECTOR3", Error);
            if (!CheckedMathValue4)
                return false;

            {
                if (!StoreVector(Context.Vectors, IntegerArgument0,
                                 VecProject3((*CheckedMathValue1), (*CheckedMathValue2), (*CheckedMathValue3),
                                             (*CheckedMathValue4),
                                             static_cast<float>(Context.Kernel->Core().WindowWidth()),
                                             static_cast<float>(Context.Kernel->Core().WindowHeight())),
                                 "PROJECT VECTOR3", Error))
                    return false;
            }
            return true;
        }

        template <typename Map>
        bool DeleteVector(Map& Vectors, int Id, const char* Command, std::string* Error = nullptr)
        {
            if (Id < 1 || Id > KMaximumMathValue)
            {
                if (Error)
                    *Error = std::string(Command) + " requires a vector number from 1 to 22000000";
                return false;
            }
            Vectors.erase(Id);
            return true;
        }

        template <typename Map>
        bool CopyVector(Map& Vectors, const std::vector<BuiltinValue>& Arguments, const char* Command,
                        std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Arguments.size() > 0 && !Arguments[0].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Arguments.size() > 1 && !Arguments[1].AsInt(IntegerArgument1, Error))
                return false;
            auto* CheckedMathValue1 = RequireVector(Vectors, IntegerArgument1, Command, Error);
            if (!CheckedMathValue1)
                return false;

            {
                if (!StoreVector(Vectors, IntegerArgument0, (*CheckedMathValue1), Command, Error))
                    return false;
            }
            return true;
        }

        template <typename Map>
        bool AddVector(Map& Vectors, const std::vector<BuiltinValue>& Arguments, const char* Command,
                       std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Arguments.size() > 0 && !Arguments[0].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Arguments.size() > 1 && !Arguments[1].AsInt(IntegerArgument1, Error))
                return false;
            int IntegerArgument2 = 0;
            if (Arguments.size() > 2 && !Arguments[2].AsInt(IntegerArgument2, Error))
                return false;
            auto* CheckedMathValue1 = RequireVector(Vectors, IntegerArgument1, Command, Error);
            if (!CheckedMathValue1)
                return false;
            auto* CheckedMathValue2 = RequireVector(Vectors, IntegerArgument2, Command, Error);
            if (!CheckedMathValue2)
                return false;

            {
                if (!StoreVector(Vectors, IntegerArgument0, VecAdd((*CheckedMathValue1), (*CheckedMathValue2)), Command,
                                 Error))
                    return false;
            }
            return true;
        }

        template <typename Map>
        bool SubtractVector(Map& Vectors, const std::vector<BuiltinValue>& Arguments, const char* Command,
                            std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Arguments.size() > 0 && !Arguments[0].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Arguments.size() > 1 && !Arguments[1].AsInt(IntegerArgument1, Error))
                return false;
            int IntegerArgument2 = 0;
            if (Arguments.size() > 2 && !Arguments[2].AsInt(IntegerArgument2, Error))
                return false;
            auto* CheckedMathValue1 = RequireVector(Vectors, IntegerArgument1, Command, Error);
            if (!CheckedMathValue1)
                return false;
            auto* CheckedMathValue2 = RequireVector(Vectors, IntegerArgument2, Command, Error);
            if (!CheckedMathValue2)
                return false;

            {
                if (!StoreVector(Vectors, IntegerArgument0, VecSub((*CheckedMathValue1), (*CheckedMathValue2)), Command,
                                 Error))
                    return false;
            }
            return true;
        }

        template <typename Map>
        bool MultiplyVector(Map& Vectors, const std::vector<BuiltinValue>& Arguments, const char* Command,
                            std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Arguments.size() > 0 && !Arguments[0].AsInt(IntegerArgument0, Error))
                return false;
            auto* CheckedMathValue1 = RequireVector(Vectors, IntegerArgument0, Command, Error);
            if (!CheckedMathValue1)
                return false;

            auto& Vector = (*CheckedMathValue1);
            float FloatArgument1 = 0.0f;
            if (Arguments.size() > 1 && !Arguments[1].AsFloat(FloatArgument1, Error))
                return false;

            Vector = VecScale(Vector, FloatArgument1);
            return true;
        }

        template <typename Map>
        bool DivideVector(Map& Vectors, const std::vector<BuiltinValue>& Arguments, const char* Command,
                          std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Arguments.size() > 0 && !Arguments[0].AsInt(IntegerArgument0, Error))
                return false;
            auto* CheckedMathValue1 = RequireVector(Vectors, IntegerArgument0, Command, Error);
            if (!CheckedMathValue1)
                return false;

            auto& Vector = (*CheckedMathValue1);
            float FloatArgument1 = 0.0f;
            if (Arguments.size() > 1 && !Arguments[1].AsFloat(FloatArgument1, Error))
                return false;

            Vector = VecScale(Vector, 1.0f / FloatArgument1);
            return true;
        }

        template <typename Map>
        bool ScaleVector(Map& Vectors, const std::vector<BuiltinValue>& Arguments, const char* Command,
                         std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Arguments.size() > 0 && !Arguments[0].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Arguments.size() > 1 && !Arguments[1].AsInt(IntegerArgument1, Error))
                return false;
            auto* CheckedMathValue1 = RequireVector(Vectors, IntegerArgument1, Command, Error);
            if (!CheckedMathValue1)
                return false;

            {
                float FloatArgument1 = 0.0f;
                if (Arguments.size() > 2 && !Arguments[2].AsFloat(FloatArgument1, Error))
                    return false;

                if (!StoreVector(Vectors, IntegerArgument0, VecScale((*CheckedMathValue1), FloatArgument1), Command,
                                 Error))
                    return false;
            }
            return true;
        }

        template <typename Map>
        bool NormalizeVector(Map& Vectors, const std::vector<BuiltinValue>& Arguments, const char* Command,
                             std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Arguments.size() > 0 && !Arguments[0].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Arguments.size() > 1 && !Arguments[1].AsInt(IntegerArgument1, Error))
                return false;
            auto* CheckedMathValue1 = RequireVector(Vectors, IntegerArgument1, Command, Error);
            if (!CheckedMathValue1)
                return false;

            {
                if (!StoreVector(Vectors, IntegerArgument0, VecNormalize((*CheckedMathValue1)), Command, Error))
                    return false;
            }
            return true;
        }

        template <typename Map>
        bool MaximizeVector(Map& Vectors, const std::vector<BuiltinValue>& Arguments, const char* Command,
                            std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Arguments.size() > 0 && !Arguments[0].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Arguments.size() > 1 && !Arguments[1].AsInt(IntegerArgument1, Error))
                return false;
            int IntegerArgument2 = 0;
            if (Arguments.size() > 2 && !Arguments[2].AsInt(IntegerArgument2, Error))
                return false;
            auto* CheckedMathValue1 = RequireVector(Vectors, IntegerArgument1, Command, Error);
            if (!CheckedMathValue1)
                return false;
            auto* CheckedMathValue2 = RequireVector(Vectors, IntegerArgument2, Command, Error);
            if (!CheckedMathValue2)
                return false;

            {
                if (!StoreVector(Vectors, IntegerArgument0, VecMaximize((*CheckedMathValue1), (*CheckedMathValue2)),
                                 Command, Error))
                    return false;
            }
            return true;
        }

        template <typename Map>
        bool MinimizeVector(Map& Vectors, const std::vector<BuiltinValue>& Arguments, const char* Command,
                            std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Arguments.size() > 0 && !Arguments[0].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Arguments.size() > 1 && !Arguments[1].AsInt(IntegerArgument1, Error))
                return false;
            int IntegerArgument2 = 0;
            if (Arguments.size() > 2 && !Arguments[2].AsInt(IntegerArgument2, Error))
                return false;
            auto* CheckedMathValue1 = RequireVector(Vectors, IntegerArgument1, Command, Error);
            if (!CheckedMathValue1)
                return false;
            auto* CheckedMathValue2 = RequireVector(Vectors, IntegerArgument2, Command, Error);
            if (!CheckedMathValue2)
                return false;

            {
                if (!StoreVector(Vectors, IntegerArgument0, VecMinimize((*CheckedMathValue1), (*CheckedMathValue2)),
                                 Command, Error))
                    return false;
            }
            return true;
        }

        template <typename Map>
        bool LerpVector(Map& Vectors, const std::vector<BuiltinValue>& Arguments, const char* Command,
                        std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Arguments.size() > 0 && !Arguments[0].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Arguments.size() > 1 && !Arguments[1].AsInt(IntegerArgument1, Error))
                return false;
            int IntegerArgument2 = 0;
            if (Arguments.size() > 2 && !Arguments[2].AsInt(IntegerArgument2, Error))
                return false;
            auto* CheckedMathValue1 = RequireVector(Vectors, IntegerArgument1, Command, Error);
            if (!CheckedMathValue1)
                return false;
            auto* CheckedMathValue2 = RequireVector(Vectors, IntegerArgument2, Command, Error);
            if (!CheckedMathValue2)
                return false;

            {
                float FloatArgument1 = 0.0f;
                if (Arguments.size() > 3 && !Arguments[3].AsFloat(FloatArgument1, Error))
                    return false;

                if (!StoreVector(Vectors, IntegerArgument0,
                                 VecLerp((*CheckedMathValue1), (*CheckedMathValue2), FloatArgument1), Command, Error))
                    return false;
            }
            return true;
        }

        template <typename Map>
        bool BarycentricVector(Map& Vectors, const std::vector<BuiltinValue>& Arguments, const char* Command,
                               std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Arguments.size() > 0 && !Arguments[0].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Arguments.size() > 1 && !Arguments[1].AsInt(IntegerArgument1, Error))
                return false;
            int IntegerArgument2 = 0;
            if (Arguments.size() > 2 && !Arguments[2].AsInt(IntegerArgument2, Error))
                return false;
            int IntegerArgument3 = 0;
            if (Arguments.size() > 3 && !Arguments[3].AsInt(IntegerArgument3, Error))
                return false;
            auto* CheckedMathValue1 = RequireVector(Vectors, IntegerArgument1, Command, Error);
            if (!CheckedMathValue1)
                return false;
            auto* CheckedMathValue2 = RequireVector(Vectors, IntegerArgument2, Command, Error);
            if (!CheckedMathValue2)
                return false;
            auto* CheckedMathValue3 = RequireVector(Vectors, IntegerArgument3, Command, Error);
            if (!CheckedMathValue3)
                return false;

            {
                float FloatArgument1 = 0.0f;
                if (Arguments.size() > 4 && !Arguments[4].AsFloat(FloatArgument1, Error))
                    return false;
                float FloatArgument2 = 0.0f;
                if (Arguments.size() > 5 && !Arguments[5].AsFloat(FloatArgument2, Error))
                    return false;

                if (!StoreVector(Vectors, IntegerArgument0,
                                 VecBarycentric((*CheckedMathValue1), (*CheckedMathValue2), (*CheckedMathValue3),
                                                FloatArgument1, FloatArgument2),
                                 Command, Error))
                    return false;
            }
            return true;
        }

        template <typename Map>
        bool CatmullRomVector(Map& Vectors, const std::vector<BuiltinValue>& Arguments, const char* Command,
                              std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Arguments.size() > 0 && !Arguments[0].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Arguments.size() > 1 && !Arguments[1].AsInt(IntegerArgument1, Error))
                return false;
            int IntegerArgument2 = 0;
            if (Arguments.size() > 2 && !Arguments[2].AsInt(IntegerArgument2, Error))
                return false;
            int IntegerArgument3 = 0;
            if (Arguments.size() > 3 && !Arguments[3].AsInt(IntegerArgument3, Error))
                return false;
            int IntegerArgument4 = 0;
            if (Arguments.size() > 4 && !Arguments[4].AsInt(IntegerArgument4, Error))
                return false;
            auto* CheckedMathValue1 = RequireVector(Vectors, IntegerArgument1, Command, Error);
            if (!CheckedMathValue1)
                return false;
            auto* CheckedMathValue2 = RequireVector(Vectors, IntegerArgument2, Command, Error);
            if (!CheckedMathValue2)
                return false;
            auto* CheckedMathValue3 = RequireVector(Vectors, IntegerArgument3, Command, Error);
            if (!CheckedMathValue3)
                return false;
            auto* CheckedMathValue4 = RequireVector(Vectors, IntegerArgument4, Command, Error);
            if (!CheckedMathValue4)
                return false;

            {
                float FloatArgument1 = 0.0f;
                if (Arguments.size() > 5 && !Arguments[5].AsFloat(FloatArgument1, Error))
                    return false;

                if (!StoreVector(Vectors, IntegerArgument0,
                                 VecCatmullRom((*CheckedMathValue1), (*CheckedMathValue2), (*CheckedMathValue3),
                                               (*CheckedMathValue4), FloatArgument1),
                                 Command, Error))
                    return false;
            }
            return true;
        }

        template <typename Map>
        bool HermiteVector(Map& Vectors, const std::vector<BuiltinValue>& Arguments, const char* Command,
                           std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Arguments.size() > 0 && !Arguments[0].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Arguments.size() > 1 && !Arguments[1].AsInt(IntegerArgument1, Error))
                return false;
            int IntegerArgument2 = 0;
            if (Arguments.size() > 2 && !Arguments[2].AsInt(IntegerArgument2, Error))
                return false;
            int IntegerArgument3 = 0;
            if (Arguments.size() > 3 && !Arguments[3].AsInt(IntegerArgument3, Error))
                return false;
            int IntegerArgument4 = 0;
            if (Arguments.size() > 4 && !Arguments[4].AsInt(IntegerArgument4, Error))
                return false;
            auto* CheckedMathValue1 = RequireVector(Vectors, IntegerArgument1, Command, Error);
            if (!CheckedMathValue1)
                return false;
            auto* CheckedMathValue2 = RequireVector(Vectors, IntegerArgument2, Command, Error);
            if (!CheckedMathValue2)
                return false;
            auto* CheckedMathValue3 = RequireVector(Vectors, IntegerArgument3, Command, Error);
            if (!CheckedMathValue3)
                return false;
            auto* CheckedMathValue4 = RequireVector(Vectors, IntegerArgument4, Command, Error);
            if (!CheckedMathValue4)
                return false;

            {
                float FloatArgument1 = 0.0f;
                if (Arguments.size() > 5 && !Arguments[5].AsFloat(FloatArgument1, Error))
                    return false;

                if (!StoreVector(Vectors, IntegerArgument0,
                                 VecHermite((*CheckedMathValue1), (*CheckedMathValue2), (*CheckedMathValue3),
                                            (*CheckedMathValue4), FloatArgument1),
                                 Command, Error))
                    return false;
            }
            return true;
        }

        bool CamId(int& Output, BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, std::size_t ValueCount,
                   std::string* Error = nullptr)
        {
            if (Args.size() > ValueCount)
                return Args.front().AsInt(Output, Error);
            Output = Ctx.Kernel->Camera().CurrentCameraId();
            return true;
        }

        const std::array<float, 3>* RequireVector3(BuiltinContext& Ctx, int VectorId, const char* Command,
                                                   std::string* Error = nullptr)
        {
            return RequireVector(Ctx.Vectors, VectorId, Command, Error);
        }

        bool StoreVector3(BuiltinContext& Ctx, int VectorId, std::array<float, 3> Value, const char* Command,
                          std::string* Error = nullptr)
        {
            {
                if (!StoreVector(Ctx.Vectors, VectorId, Value, Command, Error))
                    return false;
            }
            return true;
        }

        bool SetEffectConstantVectorCommand(BuiltinContext& Ctx, int EffectId, const std::string& Name, int VectorId,
                                            std::string* Error = nullptr)
        {
            auto* CheckedMathValue1 = RequireVector(Ctx.Vectors4, VectorId, "SET EFFECT CONSTANT VECTOR", Error);
            if (!CheckedMathValue1)
                return false;

            if (!Ctx.Kernel->Gpu().SetEffectConstantVector(EffectId, Name, (*CheckedMathValue1), Error))
                return false;
            return true;
        }

        bool SetEffectConstantMatrixCommand(BuiltinContext& Ctx, int EffectId, const std::string& Name, int MatrixId,
                                            std::string* Error = nullptr)
        {
            auto* CheckedMathValue1 = RequireMatrix4(Ctx, MatrixId, "SET EFFECT CONSTANT MATRIX", Error);
            if (!CheckedMathValue1)
                return false;

            if (!Ctx.Kernel->Gpu().SetEffectConstantMatrix(EffectId, Name, (*CheckedMathValue1), Error))
                return false;
            return true;
        }

        bool PerformChecklistForEffectValues(BuiltinContext& Ctx, int EffectId, std::string* Error = nullptr)
        {
            std::vector<GraphicsDeviceSystem::EffectParameterInfo> Parameters;
            if (!Ctx.Kernel->Gpu().EffectParameters(Parameters, EffectId, Error))
                return false;
            Ctx.Checklist.clear();
            for (const GraphicsDeviceSystem::EffectParameterInfo& Parameter : Parameters)
            {
                ChecklistItem Item;
                Item.Name = Parameter.Name;
                switch (Parameter.Kind)
                {
                    case GraphicsDeviceSystem::EffectParameterKind::Boolean:
                        Item.A = 1;
                        break;
                    case GraphicsDeviceSystem::EffectParameterKind::Integer:
                        Item.A = 2;
                        break;
                    case GraphicsDeviceSystem::EffectParameterKind::Float:
                        Item.A = 3;
                        break;
                    case GraphicsDeviceSystem::EffectParameterKind::Vector:
                        Item.A = 4;
                        break;
                    case GraphicsDeviceSystem::EffectParameterKind::Matrix:
                        Item.A = 5;
                        break;
                    case GraphicsDeviceSystem::EffectParameterKind::Opaque:
                        break;
                }
                Item.B = Parameter.Automatic ? 1 : 0;
                Item.C = Parameter.LegacyClass;
                Item.D = Parameter.LegacyType;
                Ctx.Checklist.push_back(std::move(Item));
            }
            return true;
        }

        void PerformChecklistForEffectErrors(BuiltinContext& Ctx)
        {
            Ctx.Checklist.clear();
            for (const std::string& Error : Ctx.Kernel->Gpu().EffectErrors())
            {
                ChecklistItem Item;
                Item.Name = Error;
                Ctx.Checklist.push_back(std::move(Item));
            }
        }

        bool PerformChecklistForOnscreenObjects(BuiltinContext& Context, int CameraId, std::string* Error = nullptr)
        {
            CameraSystem& Camera = Context.Kernel->Camera();
            if (Camera.CameraExist(CameraId) == 0)
            {
                if (Error)
                    *Error =
                        "PERFORM CHECKLIST FOR ONSCREEN OBJECTS requires existing camera " + std::to_string(CameraId);
                return false;
            }
            std::vector<int> Ids;
            Context.Kernel->Objects().ForEachObject(
                [&](int Id, const Object3D& Object)
                {
                    if (Object.Visible && !Object.Excluded)
                        Ids.push_back(Id);
                });
            std::sort(Ids.begin(), Ids.end());
            Context.Checklist.clear();
            for (const int Id : Ids)
            {
                int InScreen = 0;
                const int Previous = Camera.SelectRenderCamera(CameraId);
                const bool Read = Context.Kernel->Objects().ObjectInScreen(InScreen, Id, Error);
                Camera.SelectRenderCamera(Previous);
                if (!Read)
                    return false;
                if (InScreen == 0)
                    continue;
                ChecklistItem Item;
                Item.A = Id;
                Context.Checklist.push_back(std::move(Item));
            }
            return true;
        }

        bool PerformChecklistForObjectLimbs(BuiltinContext& Context, int ObjectId, std::string* Error = nullptr)
        {
            const Object3D* Object = Context.Kernel->Objects().GetObject(ObjectId);
            if (!Object)
            {
                if (Error)
                    *Error = "PERFORM CHECKLIST FOR OBJECT LIMBS requires existing object " + std::to_string(ObjectId);
                return false;
            }

            Context.Checklist.clear();
            if (!Object->LimbNodes.empty())
            {
                for (std::size_t LimbIndex = 0; LimbIndex < Object->LimbNodes.size(); ++LimbIndex)
                {
                    const aiNode* Node = Object->LimbNodes[LimbIndex];
                    const aiNode* CompatibilityRoot = Object->CompatibilityRootNode.get();
                    if (CompatibilityRoot && Node == CompatibilityRoot &&
                        !Context.Kernel->RuntimeCompatibility().IncludeSyntheticRootInLimbChecklist())
                        continue;
                    ChecklistItem Item;
                    Item.Name = Node ? Node->mName.C_Str() : "";
                    Item.A = static_cast<int>(LimbIndex);
                    Item.B = -1;
                    Item.C = -1;
                    Item.D = -1;
                    if (Node)
                    {
                        const bool IsCompatibilityRoot = Node == CompatibilityRoot;
                        const bool HasCompatibilityParent =
                            CompatibilityRoot &&
                            (Node == (Object->AssimpScene ? Object->AssimpScene->mRootNode : nullptr) ||
                             Node->mParent == Object->CompatibilitySkippedRootNode);
                        if (HasCompatibilityParent)
                        {
                            Item.B = 0;
                        }
                        else if (Node->mParent)
                        {
                            if (const auto Parent = Object->LimbIndexByNode.find(Node->mParent);
                                Parent != Object->LimbIndexByNode.end())
                                Item.B = Parent->second;
                        }
                        if (Node->mParent)
                        {
                            for (unsigned int Child = 0; Child + 1 < Node->mParent->mNumChildren; ++Child)
                            {
                                if (Node->mParent->mChildren[Child] == Node)
                                {
                                    if (const auto Sibling =
                                            Object->LimbIndexByNode.find(Node->mParent->mChildren[Child + 1]);
                                        Sibling != Object->LimbIndexByNode.end())
                                        Item.C = Sibling->second;
                                    break;
                                }
                            }
                        }
                        if (IsCompatibilityRoot && Object->AssimpScene && Object->AssimpScene->mRootNode)
                        {
                            const aiNode* FirstChild = Object->CompatibilitySkippedRootNode
                                                           ? (Object->CompatibilitySkippedRootNode->mNumChildren > 0
                                                                  ? Object->CompatibilitySkippedRootNode->mChildren[0]
                                                                  : nullptr)
                                                           : Object->AssimpScene->mRootNode;
                            if (const auto Child = Object->LimbIndexByNode.find(FirstChild);
                                Child != Object->LimbIndexByNode.end())
                                Item.D = Child->second;
                        }
                        else if (Node->mNumChildren > 0)
                        {
                            if (const auto Child = Object->LimbIndexByNode.find(Node->mChildren[0]);
                                Child != Object->LimbIndexByNode.end())
                                Item.D = Child->second;
                        }
                    }
                    Context.Checklist.push_back(std::move(Item));
                }
                return true;
            }

            const std::size_t LimbCount = std::max(Object->LimbTransforms.size(), Object->Meshes.size());
            for (std::size_t LimbIndex = 0; LimbIndex < LimbCount; ++LimbIndex)
            {
                ChecklistItem Item;
                Item.A = static_cast<int>(LimbIndex);
                Item.B = LimbIndex < Object->LimbTransforms.size() ? Object->LimbTransforms[LimbIndex].ParentLimb : -1;
                Item.C = -1;
                Item.D = -1;
                for (std::size_t Candidate = 0; Candidate < LimbCount; ++Candidate)
                {
                    const int Parent =
                        Candidate < Object->LimbTransforms.size() ? Object->LimbTransforms[Candidate].ParentLimb : -1;
                    if (Item.D == -1 && Parent == static_cast<int>(LimbIndex))
                        Item.D = static_cast<int>(Candidate);
                    if (Candidate > LimbIndex && Item.C == -1 && Parent == Item.B)
                        Item.C = static_cast<int>(Candidate);
                }
                Context.Checklist.push_back(std::move(Item));
            }
            return true;
        }

        bool StoreParticlesVector(BuiltinContext& Ctx, int VectorId, int ParticleId, bool Rotation,
                                  std::string* Error = nullptr)
        {
            std::array<float, 3> Value{};
            auto& Objects = Ctx.Kernel->Objects();
            if (Rotation)
            {
                if (!Objects.ParticlesRotationX(Value[0], ParticleId, Error) ||
                    !Objects.ParticlesRotationY(Value[1], ParticleId, Error) ||
                    !Objects.ParticlesRotationZ(Value[2], ParticleId, Error))
                    return false;
            }
            else
            {
                if (!Objects.ParticlesPositionX(Value[0], ParticleId, Error) ||
                    !Objects.ParticlesPositionY(Value[1], ParticleId, Error) ||
                    !Objects.ParticlesPositionZ(Value[2], ParticleId, Error))
                    return false;
            }
            {
                if (!StoreVector3(Ctx, VectorId, Value,
                                  Rotation ? "SET VECTOR3 TO PARTICLES ROTATION" : "SET VECTOR3 TO PARTICLES POSITION",
                                  Error))
                    return false;
            }
            return true;
        }

        bool StoreCameraVector(BuiltinContext& Ctx, int VectorId, int CameraId, bool Rotation,
                               std::string* Error = nullptr)
        {
            std::array<float, 3> Vector{};
            auto& Camera = Ctx.Kernel->Camera();
            if (Rotation)
            {
                if (!Camera.AngleX(Vector[0], CameraId, Error) || !Camera.AngleY(Vector[1], CameraId, Error) ||
                    !Camera.AngleZ(Vector[2], CameraId, Error))
                    return false;
            }
            else
            {
                if (!Camera.PositionX(Vector[0], CameraId, Error) || !Camera.PositionY(Vector[1], CameraId, Error) ||
                    !Camera.PositionZ(Vector[2], CameraId, Error))
                    return false;
            }
            {
                if (!StoreVector3(Ctx, VectorId, Vector,
                                  Rotation ? "SET VECTOR3 TO CAMERA ROTATION" : "SET VECTOR3 TO CAMERA POSITION",
                                  Error))
                    return false;
            }
            return true;
        }

        bool PositionCameraCommand(BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args,
                                   std::string* Error = nullptr)
        {
            int CameraId = 0;
            if (!CamId(CameraId, Ctx, Args, 3, Error))
                return false;
            int IntegerArgument0 = 0;
            if (Args.size() == 2 && !Args[1].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Args.size() == 2 && !Args[0].AsInt(IntegerArgument1, Error))
                return false;
            if (Args.size() == 2)
            {
                auto* CheckedMathValue1 = RequireVector3(Ctx, IntegerArgument0, "POSITION CAMERA", Error);
                if (!CheckedMathValue1)
                    return false;

                const auto& Vector = (*CheckedMathValue1);
                if (!Ctx.Kernel->Camera().PositionCamera(IntegerArgument1, Vector[0], Vector[1], Vector[2], Error))
                    return false;
                return true;
            }
            float FloatArgument1 = 0.0f;
            if (!Args[Args.size() - 3 + 0].AsFloat(FloatArgument1, Error))
                return false;
            float FloatArgument2 = 0.0f;
            if (!Args[Args.size() - 3 + 1].AsFloat(FloatArgument2, Error))
                return false;
            float FloatArgument3 = 0.0f;
            if (!Args[Args.size() - 3 + 2].AsFloat(FloatArgument3, Error))
                return false;

            if (!Ctx.Kernel->Camera().PositionCamera(CameraId, FloatArgument1, FloatArgument2, FloatArgument3, Error))
                return false;
            return true;
        }

        bool RotateCameraCommand(BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args,
                                 std::string* Error = nullptr)
        {
            int CameraId = 0;
            if (!CamId(CameraId, Ctx, Args, 3, Error))
                return false;
            int IntegerArgument0 = 0;
            if (Args.size() == 2 && !Args[1].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Args.size() == 2 && !Args[0].AsInt(IntegerArgument1, Error))
                return false;
            if (Args.size() == 2)
            {
                auto* CheckedMathValue1 = RequireVector3(Ctx, IntegerArgument0, "ROTATE CAMERA", Error);
                if (!CheckedMathValue1)
                    return false;

                const auto& Vector = (*CheckedMathValue1);
                if (!Ctx.Kernel->Camera().RotateCamera(IntegerArgument1, Vector[0], Vector[1], Vector[2], Error))
                    return false;
                return true;
            }
            float FloatArgument1 = 0.0f;
            if (!Args[Args.size() - 3 + 0].AsFloat(FloatArgument1, Error))
                return false;
            float FloatArgument2 = 0.0f;
            if (!Args[Args.size() - 3 + 1].AsFloat(FloatArgument2, Error))
                return false;
            float FloatArgument3 = 0.0f;
            if (!Args[Args.size() - 3 + 2].AsFloat(FloatArgument3, Error))
                return false;

            if (!Ctx.Kernel->Camera().RotateCamera(CameraId, FloatArgument1, FloatArgument2, FloatArgument3, Error))
                return false;
            return true;
        }

        bool PositionLightCommand(BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args,
                                  std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Args.size() == 2 && !Args[1].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument1, Error))
                return false;
            if (Args.size() == 2)
            {
                auto* CheckedMathValue1 = RequireVector3(Ctx, IntegerArgument0, "POSITION LIGHT", Error);
                if (!CheckedMathValue1)
                    return false;

                const auto& Vector = (*CheckedMathValue1);
                if (!Ctx.Kernel->Lights().PositionLight(IntegerArgument1, Vector[0], Vector[1], Vector[2], Error))
                    return false;
                return true;
            }
            float FloatArgument1 = 0.0f;
            if (Args.size() > 1 && !Args[1].AsFloat(FloatArgument1, Error))
                return false;
            float FloatArgument2 = 0.0f;
            if (Args.size() > 2 && !Args[2].AsFloat(FloatArgument2, Error))
                return false;
            float FloatArgument3 = 0.0f;
            if (Args.size() > 3 && !Args[3].AsFloat(FloatArgument3, Error))
                return false;

            if (!Ctx.Kernel->Lights().PositionLight(IntegerArgument1, FloatArgument1, FloatArgument2, FloatArgument3,
                                                    Error))
                return false;
            return true;
        }

        bool StoreMatrixVector(BuiltinContext& Ctx, int VectorId, int MatrixId, std::string* Error = nullptr)
        {
            std::array<float, 3> Vector{};
            auto& Matrices = Ctx.Kernel->Matrices();
            if (!Matrices.MatrixPositionX(Vector[0], MatrixId, Error) ||
                !Matrices.MatrixPositionY(Vector[1], MatrixId, Error) ||
                !Matrices.MatrixPositionZ(Vector[2], MatrixId, Error))
                return false;
            {
                if (!StoreVector3(Ctx, VectorId, Vector, "SET VECTOR3 TO MATRIX POSITION", Error))
                    return false;
            }
            return true;
        }

        bool PositionMatrixCommand(BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args,
                                   std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Args.size() == 2 && !Args[1].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument1, Error))
                return false;
            if (Args.size() == 2)
            {
                auto* CheckedMathValue1 = RequireVector3(Ctx, IntegerArgument0, "POSITION MATRIX", Error);
                if (!CheckedMathValue1)
                    return false;

                const auto& Vector = (*CheckedMathValue1);
                if (!Ctx.Kernel->Matrices().PositionMatrix(IntegerArgument1, Vector[0], Vector[1], Vector[2], Error))
                    return false;
                return true;
            }
            float FloatArgument1 = 0.0f;
            if (Args.size() > 1 && !Args[1].AsFloat(FloatArgument1, Error))
                return false;
            float FloatArgument2 = 0.0f;
            if (Args.size() > 2 && !Args[2].AsFloat(FloatArgument2, Error))
                return false;
            float FloatArgument3 = 0.0f;
            if (Args.size() > 3 && !Args[3].AsFloat(FloatArgument3, Error))
                return false;

            if (!Ctx.Kernel->Matrices().PositionMatrix(IntegerArgument1, FloatArgument1, FloatArgument2, FloatArgument3,
                                                       Error))
                return false;
            return true;
        }

        bool PositionParticlesCommand(BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args,
                                      std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Args.size() == 2 && !Args[1].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument1, Error))
                return false;
            if (Args.size() == 2)
            {
                auto* CheckedMathValue1 = RequireVector3(Ctx, IntegerArgument0, "POSITION PARTICLES", Error);
                if (!CheckedMathValue1)
                    return false;

                const auto& Vector = (*CheckedMathValue1);
                {
                    if (!Ctx.Kernel->Objects().PositionParticles(IntegerArgument1, Vector[0], Vector[1], Vector[2],
                                                                 Error))
                        return false;
                }
                return true;
            }
            {
                float FloatArgument1 = 0.0f;
                if (Args.size() > 1 && !Args[1].AsFloat(FloatArgument1, Error))
                    return false;
                float FloatArgument2 = 0.0f;
                if (Args.size() > 2 && !Args[2].AsFloat(FloatArgument2, Error))
                    return false;
                float FloatArgument3 = 0.0f;
                if (Args.size() > 3 && !Args[3].AsFloat(FloatArgument3, Error))
                    return false;

                if (!Ctx.Kernel->Objects().PositionParticles(IntegerArgument1, FloatArgument1, FloatArgument2,
                                                             FloatArgument3, Error))
                    return false;
            }
            return true;
        }

        bool RotateParticlesCommand(BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args,
                                    std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Args.size() == 2 && !Args[1].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument1, Error))
                return false;
            if (Args.size() == 2)
            {
                auto* CheckedMathValue1 = RequireVector3(Ctx, IntegerArgument0, "ROTATE PARTICLES", Error);
                if (!CheckedMathValue1)
                    return false;

                const auto& Vector = (*CheckedMathValue1);
                {
                    if (!Ctx.Kernel->Objects().RotateParticles(IntegerArgument1, Vector[0], Vector[1], Vector[2],
                                                               Error))
                        return false;
                }
                return true;
            }
            {
                float FloatArgument1 = 0.0f;
                if (Args.size() > 1 && !Args[1].AsFloat(FloatArgument1, Error))
                    return false;
                float FloatArgument2 = 0.0f;
                if (Args.size() > 2 && !Args[2].AsFloat(FloatArgument2, Error))
                    return false;
                float FloatArgument3 = 0.0f;
                if (Args.size() > 3 && !Args[3].AsFloat(FloatArgument3, Error))
                    return false;

                if (!Ctx.Kernel->Objects().RotateParticles(IntegerArgument1, FloatArgument1, FloatArgument2,
                                                           FloatArgument3, Error))
                    return false;
            }
            return true;
        }

        bool RotateLightCommand(BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args,
                                std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Args.size() == 2 && !Args[1].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument1, Error))
                return false;
            if (Args.size() == 2)
            {
                auto* CheckedMathValue1 = RequireVector3(Ctx, IntegerArgument0, "ROTATE LIGHT", Error);
                if (!CheckedMathValue1)
                    return false;

                const auto& Vector = (*CheckedMathValue1);
                if (!Ctx.Kernel->Lights().RotateLight(IntegerArgument1, Vector[0], Vector[1], Vector[2], Error))
                    return false;
                return true;
            }
            float FloatArgument1 = 0.0f;
            if (Args.size() > 1 && !Args[1].AsFloat(FloatArgument1, Error))
                return false;
            float FloatArgument2 = 0.0f;
            if (Args.size() > 2 && !Args[2].AsFloat(FloatArgument2, Error))
                return false;
            float FloatArgument3 = 0.0f;
            if (Args.size() > 3 && !Args[3].AsFloat(FloatArgument3, Error))
                return false;

            if (!Ctx.Kernel->Lights().RotateLight(IntegerArgument1, FloatArgument1, FloatArgument2, FloatArgument3,
                                                  Error))
                return false;
            return true;
        }

        template <typename Map> BuiltinValue MakeVectorValue(BuiltinContext& Context, Map& Vectors, int Id)
        {
            if (Id < 1 || Id > KMaximumMathValue)
                return BuiltinValue::FromNumber(0.0);
            ClearMathValue(Context, Id);
            Vectors[Id] = typename Map::mapped_type{};
            return BuiltinValue::FromNumber(1.0);
        }

        template <typename Map> BuiltinValue DeleteVectorValue(Map& Vectors, int Id)
        {
            if (Id < 1 || Id > KMaximumMathValue)
                return BuiltinValue::FromNumber(0.0);
            return BuiltinValue::FromNumber(Vectors.erase(Id) != 0 ? 1.0 : 0.0);
        }

        BuiltinValue MakeMatrix4Value(BuiltinContext& Context, int Id)
        {
            if (Id < 1 || Id > KMaximumMathValue)
                return BuiltinValue::FromNumber(0.0);
            ClearMathValue(Context, Id);
            Context.Matrices4[Id] = Mat4{};
            return BuiltinValue::FromNumber(1.0);
        }

        BuiltinValue DeleteMatrix4Value(BuiltinContext& Context, int Id)
        {
            if (Id < 1 || Id > KMaximumMathValue)
                return BuiltinValue::FromNumber(0.0);
            return BuiltinValue::FromNumber(Context.Matrices4.erase(Id) != 0 ? 1.0 : 0.0);
        }

        BuiltinValue InverseMatrix4Value(BuiltinContext& Ctx, int ResultId, int SourceId, std::string* Error = nullptr)
        {
            auto* CheckedMathValue1 = RequireMatrix4(Ctx, SourceId, "INVERSE MATRIX4", Error);
            if (!CheckedMathValue1)
                return BuiltinValue{};

            const Mat4 Source = (*CheckedMathValue1);
            const float Determinant = Mat4Determinant(Source);
            {
                if (!StoreMatrix4(Ctx, ResultId, Mat4Inverse(Source), "INVERSE MATRIX4", Error))
                    return BuiltinValue{};
            }
            return BuiltinValue::FromNumber(static_cast<double>(Determinant));
        }

        Mat4 CameraViewMatrix(const CameraSystem& Camera)
        {
            Mat4 View = Mat4Translation(-Camera.PositionX(), -Camera.PositionY(), -Camera.PositionZ());
            if (Camera.IsZyxRotation())
            {
                View = Mat4Multiply(View, Mat4RotationX(Mat4DegreesToRadians(-Camera.AngleX())));
                View = Mat4Multiply(View, Mat4RotationY(Mat4DegreesToRadians(-Camera.AngleY())));
            }
            else
            {
                View = Mat4Multiply(View, Mat4RotationY(Mat4DegreesToRadians(-Camera.AngleY())));
                View = Mat4Multiply(View, Mat4RotationX(Mat4DegreesToRadians(-Camera.AngleX())));
            }
            return Mat4Multiply(View, Mat4RotationZ(Mat4DegreesToRadians(-Camera.AngleZ())));
        }

        Mat4 CameraProjectionMatrix(const CameraSystem& Camera)
        {
            return Mat4PerspectiveFov(Mat4DegreesToRadians(Camera.Fov()), Camera.Aspect(), Camera.ClampedNearPlane(),
                                      Camera.ClampedFarPlane(), true);
        }

        fs::path ResolveNativeAssetPath(const BuiltinContext& Ctx, const std::string& Input)
        {
            return ResolveBasicPath(Ctx.AssetRoot, dbc::PathFromUtf8(Input));
        }

        std::string ResolveAssetPath(const BuiltinContext& Ctx, const std::string& Input)
        {
            return dbc::PathToUtf8(ResolveNativeAssetPath(Ctx, Input));
        }

        // DBPro tried the map name as given, then the same name in the pk3's folder.
        bool LoadBspCommand(BuiltinContext& Ctx, const std::string& Pk3, const std::string& MapName, std::string* Error)
        {
            std::vector<std::string> Candidates{ResolveAssetPath(Ctx, MapName)};
            const std::size_t Separator = Pk3.find_last_of("/\\");
            if (Separator != std::string::npos)
                Candidates.push_back(ResolveAssetPath(Ctx, Pk3.substr(0, Separator + 1) + MapName));
            return Ctx.Kernel->BspWorld().Load(Pk3.empty() ? std::string{} : ResolveAssetPath(Ctx, Pk3), Candidates,
                                               Error);
        }

        bool LoadEffectCommand(BuiltinContext& Context, const std::vector<BuiltinValue>& Arguments,
                               std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Arguments.size() > 1 && !Arguments[1].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Arguments.size() > 2 && !Arguments[2].AsInt(IntegerArgument1, Error))
                return false;
            int IntegerArgument2 = 0;
            if (Arguments.size() > 3 && !Arguments[3].AsInt(IntegerArgument2, Error))
                return false;
            // DBPro reports LOAD EFFECT failures through EFFECT EXIST and the effect error checklist.
            std::string LoadError;
            if (!Context.Kernel->Gpu().LoadEffect(ResolveAssetPath(Context, Arguments[0].AsString()), IntegerArgument0,
                                                  IntegerArgument1, Arguments.size() > 3 ? IntegerArgument2 : 0,
                                                  &LoadError))
                DBC_LOG_WARN("LOAD EFFECT: {}", LoadError);
            return true;
        }

        // PLAY ANIMATION takes the animation alone, a position, an area or a bitmap and an area; LOOP ANIMATION
        // takes the first and last forms.
        bool PlayAnimationCommand(BuiltinContext& Context, const std::vector<BuiltinValue>& Arguments, bool Loop,
                                  std::string* Error = nullptr)
        {
            std::array<int, 6> Values{};
            for (std::size_t Index = 0; Index < Arguments.size() && Index < Values.size(); ++Index)
                if (!Arguments[Index].AsInt(Values[Index], Error))
                    return false;
            VideoSystem& Video = Context.Kernel->Video();
            switch (Arguments.size())
            {
                case 3:
                    return Video.PlayAnimationAt(Values[0], Values[1], Values[2], Error);
                case 5:
                    return Video.PlayAnimation(Values[0], Loop, std::nullopt,
                                               AnimationArea{Values[1], Values[2], Values[3], Values[4]}, Error);
                case 6:
                    return Video.PlayAnimation(Values[0], Loop, Values[1],
                                               AnimationArea{Values[2], Values[3], Values[4], Values[5]}, Error);
                default:
                    return Video.PlayAnimation(Values[0], Loop, std::nullopt, std::nullopt, Error);
            }
        }

        // Strings, BASIC integers and reals reach the plugin with their own types; integers keep their 32 bits,
        // so GET MEMBLOCK PTR addresses above 2^31 pass unchanged.
        bool CallDll(BuiltinContext& Context, const std::vector<BuiltinValue>& Arguments, DllSystem::Value& Result,
                     std::string* Error)
        {
            int Number = 0;
            if (!Arguments[0].AsInt(Number, Error))
                return false;
            std::vector<DllSystem::Value> Values;
            Values.reserve(Arguments.size() - 2);
            for (std::size_t Index = 2; Index < Arguments.size(); ++Index)
            {
                const BuiltinValue& Source = Arguments[Index];
                DllSystem::Value Value;
                if (Source.IsString)
                {
                    Value.Type = DllSystem::ValueType::String;
                    Value.Text = Source.Text;
                }
                else if (Source.IsInteger)
                {
                    std::uint32_t Bits = 0;
                    if (!Source.AsDword(Bits, Error))
                        return false;
                    Value.Type = DllSystem::ValueType::Integer;
                    Value.Integer = static_cast<std::int32_t>(Bits);
                }
                else
                {
                    Value.Type = DllSystem::ValueType::Real;
                    Value.Real = Source.Number;
                }
                Values.push_back(std::move(Value));
            }
            return Context.Kernel->Dlls().CallDll(Result, Number, Arguments[1].AsString(), Values, Error);
        }

        struct BackbufferLayout
        {
            int Width = 0;
            int Height = 0;
            int Depth = 0;
            int Pitch = 0;
        };

        // The locked buffer while LOCK BACKBUFFER holds it, otherwise the screen it would copy.
        BackbufferLayout Backbuffer(BuiltinContext& Context)
        {
            BackbufferLayout Layout;
            if (Context.Kernel->Memblocks().LockedBackbuffer(Layout.Width, Layout.Height, Layout.Depth, Layout.Pitch))
                return Layout;
            CoreSystem& Core = Context.Kernel->Core();
            Layout.Width = Core.WindowWidth();
            Layout.Height = Core.WindowHeight();
            Layout.Depth = Core.ScreenDepth();
            Layout.Pitch = Layout.Width * (Layout.Depth / 8);
            return Layout;
        }

        const std::filesystem::path& RegistryPath(BuiltinContext& Context)
        {
            if (Context.RegistryPath.empty())
                Context.RegistryPath = DefaultRegistryPath();
            return Context.RegistryPath;
        }

        bool ExecutablePath(std::string& Output, std::string* Error = nullptr)
        {
#if defined(__APPLE__)
            std::uint32_t Size = 0;
            _NSGetExecutablePath(nullptr, &Size);
            std::vector<char> Buffer(Size);
            if (_NSGetExecutablePath(Buffer.data(), &Size) != 0)
            {
                if (Error)
                    *Error = "APPNAME$ cannot determine the executable path";
                return false;
            }
            std::error_code OperationError;
            const fs::path Canonical = fs::weakly_canonical(dbc::PathFromUtf8(Buffer.data()), OperationError);
            {
                Output = OperationError ? std::string(Buffer.data()) : dbc::PathToUtf8(Canonical);
                return true;
            }
#elif defined(_WIN32)
            std::vector<char> Buffer(32768);
            const DWORD Size = GetModuleFileNameA(nullptr, Buffer.data(), static_cast<DWORD>(Buffer.size()));
            if (Size == 0 || Size == Buffer.size())
            {
                if (Error)
                    *Error = "APPNAME$ cannot determine the executable path";
                return false;
            }
            {
                Output = std::string(Buffer.data(), Size);
                return true;
            }
#else
            std::vector<char> Buffer(4096);
            const ssize_t Size = readlink("/proc/self/exe", Buffer.data(), Buffer.size());
            if (Size <= 0 || static_cast<std::size_t>(Size) == Buffer.size())
            {
                if (Error)
                    *Error = "APPNAME$ cannot determine the executable path";
                return false;
            }
            {
                Output = std::string(Buffer.data(), static_cast<std::size_t>(Size));
                return true;
            }
#endif
        }

        bool DocumentsDirectory(std::string& Output, std::string* Error = nullptr)
        {
            const char* Documents = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
            if (!Documents)
            {
                if (Error)
                    *Error = std::string("MYDOCDIR$ failed: ") + SDL_GetError();
                return false;
            }
            {
                Output = Documents;
                return true;
            }
        }

        double FileSize(BuiltinContext& Ctx, const std::string& Input)
        {
            std::error_code Error;
            const std::uintmax_t Size = fs::file_size(ResolveNativeAssetPath(Ctx, Input), Error);
            return Error ? 0.0 : static_cast<double>(Size);
        }

        bool ReadByteFromFile(double& Output, BuiltinContext& Ctx, const std::string& Input, int Position,
                              std::string* Error = nullptr)
        {
            const fs::path Path = ResolveNativeAssetPath(Ctx, Input);
            if (Position < 0)
            {
                Output = 0.0;
                return true;
            }
            std::ifstream File(Path, std::ios::binary);
            if (!File)
            {
                if (Error)
                    *Error = "READ BYTE FROM FILE cannot open '" + dbc::PathToUtf8(Path) + "'";
                return false;
            }
            File.seekg(Position);
            std::uint8_t Byte = 0;
            File.read(reinterpret_cast<char*>(&Byte), 1);
            {
                Output = File.gcount() == 1 ? static_cast<double>(Byte) : 0.0;
                return true;
            }
        }

        bool RequireDirectory(fs::path& Output, BuiltinContext& Ctx, const std::string& Input, std::string_view Command,
                              std::string* Error = nullptr)
        {
            const fs::path Path = ResolveNativeAssetPath(Ctx, Input);
            std::error_code OperationError;
            if (!fs::is_directory(Path, OperationError))
            {
                if (Error)
                    *Error = std::string(Command) + " requires an existing directory: '" + dbc::PathToUtf8(Path) + "'";
                return false;
            }
            {
                Output = Path;
                return true;
            }
        }

        bool RenameFile(BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, std::string_view Command,
                        std::string* Error = nullptr)
        {
            const fs::path Source = ResolveNativeAssetPath(Ctx, Args[0].AsString());
            const fs::path Destination = ResolveNativeAssetPath(Ctx, Args[1].AsString());
            std::error_code OperationError;
            if (!fs::is_regular_file(Source, OperationError))
            {
                if (Error)
                    *Error = std::string(Command) + " source does not exist: '" + dbc::PathToUtf8(Source) + "'";
                return false;
            }
            OperationError.clear();
            if (fs::exists(Destination, OperationError))
            {
                if (Error)
                    *Error = std::string(Command) + " refuses to overwrite: '" + dbc::PathToUtf8(Destination) + "'";
                return false;
            }
            fs::rename(Source, Destination, OperationError);
            if (OperationError)
            {
                if (Error)
                    *Error = std::string(Command) + " failed: " + OperationError.message();
                return false;
            }
            return true;
        }

        bool LimbTextureImageId(int& Output, BuiltinContext& Context, const std::vector<BuiltinValue>& Arguments,
                                std::string* Error = nullptr)
        {
            int IntegerArgument0 = 0;
            if (Arguments.size() > 0 && !Arguments[0].AsInt(IntegerArgument0, Error))
                return false;
            int IntegerArgument1 = 0;
            if (Arguments.size() > 1 && !Arguments[1].AsInt(IntegerArgument1, Error))
                return false;
            int IntegerArgument2 = 0;
            if (Arguments.size() > 2 && !Arguments[2].AsInt(IntegerArgument2, Error))
                return false;
            const Object3D* Object = Context.Kernel->Objects().GetObject(IntegerArgument0);
            if (!Object)
            {
                Output = 0;
                return true;
            }
            const int LimbId = IntegerArgument1;
            const Mesh* Limb = Context.Kernel->Renderer().FindLimbMesh(*Object, LimbId);
            if (!Limb)
            {
                Output = 0;
                return true;
            }
            const int Stage = Arguments.size() > 2 ? IntegerArgument2 : 0;
            if (Stage == 0)
            {
                Output = Limb->TextureImageId;
                return true;
            }
            const auto Texture = Limb->TextureStageImageIds.find(Stage);
            {
                Output = Texture == Limb->TextureStageImageIds.end() ? 0 : Texture->second;
                return true;
            }
        }

        bool LimbTextureName(std::string& Output, BuiltinContext& Context, const std::vector<BuiltinValue>& Arguments,
                             std::string* Error = nullptr)
        {
            int ImageId = 0;
            if (!LimbTextureImageId(ImageId, Context, Arguments, Error))
                return false;
            const Image* Texture = ImageId == 0 ? nullptr : Context.Kernel->ImageStoreRef().GetImage(ImageId);
            {
                Output = Texture ? Texture->SourcePath : std::string{};
                return true;
            }
        }

        bool WriteHostLine(BuiltinContext& Context, const std::string& Value, std::string* Error = nullptr)
        {
            if (Context.Output)
            {
                try
                {
                    Context.Output(Value);
                }
                catch (const std::exception& Exception)
                {
                    if (Error)
                        *Error = std::string("Host output failed: ") + Exception.what();
                    return false;
                }
                catch (...)
                {
                    if (Error)
                        *Error = "Host output failed with an unknown exception";
                    return false;
                }
            }
            else if (std::fprintf(stdout, "%s\n", Value.c_str()) < 0 || std::fflush(stdout) != 0)
            {
                if (Error)
                    *Error = "Could not write standard output";
                return false;
            }
            if (Context.LogFile &&
                (std::fprintf(Context.LogFile, "%s\n", Value.c_str()) < 0 || std::fflush(Context.LogFile) != 0))
            {
                if (Error)
                    *Error = "Could not write the BASIC log file";
                return false;
            }
            return true;
        }

        bool EmitLine(BuiltinContext& Context, const std::string& Value, std::string* Error = nullptr)
        {
            Print(*Context.Kernel, Value);
            return WriteHostLine(Context, Value, Error);
        }

        bool DrivePaths(std::vector<fs::path>& Output, [[maybe_unused]] std::string* Error = nullptr)
        {
            std::vector<fs::path> Result;
#if defined(_WIN32)
            for (char Letter = 'A'; Letter <= 'Z'; ++Letter)
            {
                const fs::path Candidate = std::string(1, Letter) + ":\\";
                std::error_code ScanError;
                if (fs::is_directory(Candidate, ScanError))
                    Result.push_back(Candidate);
            }
#else
            Result.emplace_back("/");
#if defined(__APPLE__)
            constexpr std::string_view MountRoot = "/Volumes";
#else
            constexpr std::string_view MountRoot = "/mnt";
#endif
            std::error_code ScanError;
            for (fs::directory_iterator It(dbc::PathFromUtf8(MountRoot), ScanError), End; !ScanError && It != End;
                 It.increment(ScanError))
            {
                std::error_code TypeError;
                const bool Directory = It->is_directory(TypeError);
                if (TypeError)
                {
                    if (Error)
                        *Error = "Could not inspect a mounted drive: " + TypeError.message();
                    return false;
                }
                if (Directory)
                    Result.push_back(It->path());
            }
            if (ScanError && ScanError != std::errc::no_such_file_or_directory)
            {
                if (Error)
                    *Error = "Could not enumerate mounted drives: " + ScanError.message();
                return false;
            }
#endif
            std::sort(Result.begin(), Result.end());
            Result.erase(std::unique(Result.begin(), Result.end()), Result.end());
            Output = std::move(Result);
            return true;
        }

        bool DirectoryList(BuiltinContext& Context, std::string* Error = nullptr)
        {
            std::vector<std::string> Names;
            std::error_code OperationError;
            for (fs::directory_iterator It(Context.AssetRoot, OperationError), End; !OperationError && It != End;
                 It.increment(OperationError))
                Names.push_back(dbc::PathToUtf8(It->path().filename()));
            if (OperationError)
            {
                if (Error)
                    *Error = "DIR failed for '" + dbc::PathToUtf8(Context.AssetRoot) + "': " + OperationError.message();
                return false;
            }
            std::sort(Names.begin(), Names.end());
            for (const std::string& Name : Names)
                if (!EmitLine(Context, Name, Error))
                    return false;
            return true;
        }

        bool PerformChecklistForDrives(BuiltinContext& Context, std::string* Error = nullptr)
        {
            Context.Checklist.clear();
            std::vector<fs::path> Paths;
            if (!DrivePaths(Paths, Error))
                return false;
            for (const fs::path& Path : Paths)
            {
                ChecklistItem Item;
                Item.Name = dbc::PathToUtf8(Path);
                Context.Checklist.push_back(std::move(Item));
            }
            return true;
        }

        bool DriveList(BuiltinContext& Context, std::string* Error = nullptr)
        {
            std::vector<fs::path> Paths;
            if (!DrivePaths(Paths, Error))
                return false;
            for (const fs::path& Path : Paths)
                if (!EmitLine(Context, dbc::PathToUtf8(Path), Error))
                    return false;
            return true;
        }

        bool ExecuteFile(BuiltinContext& Context, const std::vector<BuiltinValue>& Arguments,
                         std::string* Error = nullptr)
        {
            if (!Context.LaunchFile)
            {
                if (Error)
                    *Error = "EXECUTE FILE has no host launcher";
                return false;
            }
            HostFileLaunchRequest Request;
            Request.FileName = ResolveNativeAssetPath(Context, Arguments[0].AsString());
            Request.CommandLine = Arguments[1].AsString();
            Request.WorkingDirectory = Context.AssetRoot;
            if (!Arguments[2].AsString().empty() &&
                !RequireDirectory(Request.WorkingDirectory, Context, Arguments[2].AsString(), "EXECUTE FILE", Error))
                return false;
            Request.WaitForTermination = Arguments.size() > 3 && Arguments[3].AsBool();
            std::string OperationError;
            bool Launched = false;
            try
            {
                Launched = Context.LaunchFile(Request, OperationError);
            }
            catch (const std::exception& Exception)
            {
                OperationError = Exception.what();
            }
            catch (...)
            {
                OperationError = "Host launcher failed with an unknown exception";
            }
            if (!Launched)
            {
                if (Context.Profile == language::LanguageProfile::Dbc)
                {
                    DBC_LOG_DEBUG("EXECUTE FILE ignored unavailable Classic host target '{}': {}",
                                  dbc::PathToUtf8(Request.FileName), OperationError);
                    return true;
                }
                {
                    if (Error)
                        *Error = OperationError.empty() ? "EXECUTE FILE failed" : std::move(OperationError);
                    return false;
                }
            }
            return true;
        }

        void ExitPrompt(BuiltinContext& Context, const std::vector<BuiltinValue>& Arguments)
        {
            Context.PendingExitPrompt = std::pair{Arguments[0].AsString(), Arguments[1].AsString()};
            Context.ExitPromptPresented = false;
        }

        constexpr std::array<std::uint8_t, 8> KDirblockMagic = {'O', 'D', 'B', 'D', 'I', 'R', '0', '1'};
        constexpr int KMaximumDirblockEntries = 65'535;
        constexpr int KMaximumDirblockPathBytes = 32'768;
        constexpr int KMaximumDirblockFileBytes = 256 * 1024 * 1024;
        constexpr std::uint64_t KMaximumDirblockAggregateBytes = UINT64_C(1024) * 1024 * 1024;
        constexpr std::uint64_t KMaximumDirblockAggregatePathBytes = UINT64_C(64) * 1024 * 1024;

        bool IsConfinedTo(const fs::path& Root, const fs::path& Candidate)
        {
            const fs::path Relative = Candidate.lexically_normal().lexically_relative(Root.lexically_normal());
            return !Relative.empty() && !Relative.is_absolute() && *Relative.begin() != "..";
        }

        bool ConfinedCanonicalPath(fs::path& Output, const BuiltinContext& Context, const fs::path& Candidate,
                                   std::string_view Command, std::string* Error = nullptr)
        {
            if (Context.AssetRoot.empty())
            {
                if (Error)
                    *Error = std::string(Command) + " requires an execution asset root";
                return false;
            }
            std::error_code OperationError;
            const fs::path AssetRoot = fs::weakly_canonical(Context.AssetRoot, OperationError);
            if (OperationError)
            {
                if (Error)
                    *Error = std::string(Command) + " cannot resolve the execution asset root";
                return false;
            }
            const fs::path Canonical = fs::weakly_canonical(Candidate, OperationError);
            if (OperationError || !IsConfinedTo(AssetRoot, Canonical))
            {
                if (Error)
                    *Error = std::string(Command) + " path must remain inside the execution asset root";
                return false;
            }
            {
                Output = Canonical;
                return true;
            }
        }

        bool WriteDirblock(BuiltinContext& Context, int FileNumber, const std::string& Folder,
                           std::string* Error = nullptr)
        {
            fs::path Source;
            if (!RequireDirectory(Source, Context, Folder, "WRITE DIRBLOCK", Error))
                return false;
            std::error_code FileError;
            const fs::file_status SourceStatus = fs::symlink_status(Source, FileError);
            if (FileError || fs::is_symlink(SourceStatus))
            {
                if (Error)
                    *Error = "WRITE DIRBLOCK refuses a symbolic-link root";
                return false;
            }
            fs::path Root;
            if (!ConfinedCanonicalPath(Root, Context, Source, "WRITE DIRBLOCK", Error))
                return false;
            std::vector<fs::directory_entry> Entries;
            for (fs::recursive_directory_iterator It(Root, fs::directory_options::none, FileError), End;
                 !FileError && It != End; It.increment(FileError))
            {
                const fs::file_status Status = It->symlink_status(FileError);
                if (FileError)
                    break;
                if (fs::is_symlink(Status))
                {
                    if (Error)
                        *Error = "WRITE DIRBLOCK refuses symbolic links: '" + dbc::PathToUtf8(It->path()) + "'";
                    return false;
                }
                if (fs::is_directory(Status) || fs::is_regular_file(Status))
                    Entries.push_back(*It);
                if (Entries.size() > static_cast<std::size_t>(KMaximumDirblockEntries))
                {
                    if (Error)
                        *Error = "WRITE DIRBLOCK directory contains too many entries";
                    return false;
                }
            }
            if (FileError)
            {
                if (Error)
                    *Error = "WRITE DIRBLOCK cannot scan '" + dbc::PathToUtf8(Root) + "': " + FileError.message();
                return false;
            }
            std::sort(Entries.begin(), Entries.end(), [](const auto& Left, const auto& Right)
                      { return dbc::GenericPathToUtf8(Left.path()) < dbc::GenericPathToUtf8(Right.path()); });

            std::uint64_t AggregateBytes = 0;
            std::uint64_t AggregatePathBytes = 0;
            for (const fs::directory_entry& Entry : Entries)
            {
                const std::string Relative = dbc::GenericPathToUtf8(Entry.path().lexically_relative(Root));
                if (Relative.empty() || Relative.size() > static_cast<std::size_t>(KMaximumDirblockPathBytes) ||
                    Relative.size() > KMaximumDirblockAggregatePathBytes - AggregatePathBytes)
                {
                    if (Error)
                        *Error = "WRITE DIRBLOCK exceeded its portable path-data limit";
                    return false;
                }
                AggregatePathBytes += Relative.size();
                if (Entry.is_regular_file(FileError))
                {
                    const std::uintmax_t Size = Entry.file_size(FileError);
                    if (FileError || Size > static_cast<std::uintmax_t>(KMaximumDirblockFileBytes) ||
                        Size > KMaximumDirblockAggregateBytes - AggregateBytes)
                    {
                        if (Error)
                            *Error = "WRITE DIRBLOCK exceeded its portable content limit";
                        return false;
                    }
                    AggregateBytes += Size;
                }
                if (FileError)
                {
                    if (Error)
                        *Error = "WRITE DIRBLOCK cannot inspect '" + dbc::PathToUtf8(Entry.path()) + "'";
                    return false;
                }
            }

            if (!Context.Kernel->FileIO().FileWriteBytes(FileNumber, KDirblockMagic, Error))
                return false;
            if (!Context.Kernel->FileIO().FileWriteLong(FileNumber, static_cast<int>(Entries.size()), Error))
                return false;
            for (const fs::directory_entry& Entry : Entries)
            {
                const std::string Relative = dbc::GenericPathToUtf8(Entry.path().lexically_relative(Root));
                if (Relative.empty() || Relative.size() > static_cast<std::size_t>(KMaximumDirblockPathBytes))
                {
                    if (Error)
                        *Error = "WRITE DIRBLOCK encountered an invalid relative path";
                    return false;
                }
                const std::vector<std::uint8_t> PathBytes(Relative.begin(), Relative.end());
                if (!Context.Kernel->FileIO().FileWriteLong(FileNumber, static_cast<int>(PathBytes.size()), Error))
                    return false;
                if (!Context.Kernel->FileIO().FileWriteBytes(FileNumber, PathBytes, Error))
                    return false;
                const bool Directory = Entry.is_directory(FileError);
                if (FileError)
                {
                    if (Error)
                        *Error = "WRITE DIRBLOCK cannot inspect '" + dbc::PathToUtf8(Entry.path()) + "'";
                    return false;
                }
                if (!Context.Kernel->FileIO().FileWriteLong(FileNumber, Directory ? 1 : 0, Error))
                    return false;
                if (Directory)
                    continue;
                const std::uintmax_t Size = Entry.file_size(FileError);
                if (FileError || Size > static_cast<std::uintmax_t>(KMaximumDirblockFileBytes))
                {
                    if (Error)
                        *Error =
                            "WRITE DIRBLOCK file is unreadable or too large: '" + dbc::PathToUtf8(Entry.path()) + "'";
                    return false;
                }
                std::ifstream Input(Entry.path(), std::ios::binary);
                std::vector<std::uint8_t> Bytes(static_cast<std::size_t>(Size));
                if (!Input || (!Bytes.empty() && !Input.read(reinterpret_cast<char*>(Bytes.data()),
                                                             static_cast<std::streamsize>(Bytes.size()))))
                {
                    if (Error)
                        *Error = "WRITE DIRBLOCK cannot read '" + dbc::PathToUtf8(Entry.path()) + "'";
                    return false;
                }
                if (!Context.Kernel->FileIO().FileWriteLong(FileNumber, static_cast<int>(Bytes.size()), Error))
                    return false;
                if (!Context.Kernel->FileIO().FileWriteBytes(FileNumber, Bytes, Error))
                    return false;
            }
            return true;
        }

        bool ReadDirblock(BuiltinContext& Context, int FileNumber, const std::string& Folder,
                          std::string* Error = nullptr)
        {
            std::error_code FileError;
            const fs::path ResolvedDestination = fs::absolute(ResolveNativeAssetPath(Context, Folder), FileError);
            if (FileError)
            {
                if (Error)
                    *Error = "READ DIRBLOCK cannot resolve its destination";
                return false;
            }
            const fs::file_status DestinationStatus = fs::symlink_status(ResolvedDestination, FileError);
            if (!FileError && fs::is_symlink(DestinationStatus))
            {
                if (Error)
                    *Error = "READ DIRBLOCK refuses a symbolic-link destination root";
                return false;
            }
            FileError.clear();
            fs::path Destination;
            if (!ConfinedCanonicalPath(Destination, Context, ResolvedDestination, "READ DIRBLOCK", Error))
                return false;
            std::vector<std::uint8_t> Magic;
            if (!Context.Kernel->FileIO().FileReadBytes(Magic, FileNumber, KDirblockMagic.size(), Error))
                return false;
            if (!std::equal(Magic.begin(), Magic.end(), KDirblockMagic.begin()))
            {
                if (Error)
                    *Error = "READ DIRBLOCK found an invalid pack header";
                return false;
            }
            int Count{};
            if (!Context.Kernel->FileIO().FileReadLong(Count, FileNumber, Error))
                return false;
            if (Count < 0 || Count > KMaximumDirblockEntries)
            {
                if (Error)
                    *Error = "READ DIRBLOCK found an invalid entry count";
                return false;
            }
            fs::create_directories(Destination, FileError);
            if (FileError)
            {
                if (Error)
                    *Error = "READ DIRBLOCK cannot create '" + dbc::PathToUtf8(Destination) + "'";
                return false;
            }

            std::uint64_t AggregateBytes = 0;
            std::uint64_t AggregatePathBytes = 0;
            for (int Index = 0; Index < Count; ++Index)
            {
                int PathSize{};
                if (!Context.Kernel->FileIO().FileReadLong(PathSize, FileNumber, Error))
                    return false;
                if (PathSize <= 0 || PathSize > KMaximumDirblockPathBytes)
                {
                    if (Error)
                        *Error = "READ DIRBLOCK found an invalid path length";
                    return false;
                }
                if (static_cast<std::uint64_t>(PathSize) > KMaximumDirblockAggregatePathBytes - AggregatePathBytes)
                {
                    if (Error)
                        *Error = "READ DIRBLOCK exceeded its portable path-data limit";
                    return false;
                }
                AggregatePathBytes += static_cast<std::uint64_t>(PathSize);
                std::vector<std::uint8_t> PathBytes;
                if (!Context.Kernel->FileIO().FileReadBytes(PathBytes, FileNumber, static_cast<std::size_t>(PathSize),
                                                            Error))
                    return false;
                const fs::path Relative =
                    NormalizeBasicPath(dbc::PathFromUtf8(std::string(PathBytes.begin(), PathBytes.end())))
                        .lexically_normal();
                if (Relative.empty() || Relative.is_absolute() || *Relative.begin() == "..")
                {
                    if (Error)
                        *Error = "READ DIRBLOCK found an unsafe entry path";
                    return false;
                }
                const fs::path Output = (Destination / Relative).lexically_normal();
                if (!IsConfinedTo(Destination, Output))
                {
                    if (Error)
                        *Error = "READ DIRBLOCK entry escapes its destination";
                    return false;
                }
                int Directory{};
                if (!Context.Kernel->FileIO().FileReadLong(Directory, FileNumber, Error))
                    return false;
                if (Directory == 1)
                {
                    const fs::path CanonicalOutput = fs::weakly_canonical(Output, FileError);
                    if (FileError || !IsConfinedTo(Destination, CanonicalOutput))
                    {
                        if (Error)
                            *Error = "READ DIRBLOCK directory resolves outside its destination";
                        return false;
                    }
                    FileError.clear();
                    fs::create_directories(Output, FileError);
                    if (FileError)
                    {
                        if (Error)
                            *Error = "READ DIRBLOCK cannot create '" + dbc::PathToUtf8(Output) + "'";
                        return false;
                    }
                    continue;
                }
                if (Directory != 0)
                {
                    if (Error)
                        *Error = "READ DIRBLOCK found an invalid entry type";
                    return false;
                }
                int Size{};
                if (!Context.Kernel->FileIO().FileReadLong(Size, FileNumber, Error))
                    return false;
                if (Size < 0 || Size > KMaximumDirblockFileBytes)
                {
                    if (Error)
                        *Error = "READ DIRBLOCK found an invalid file size";
                    return false;
                }
                if (static_cast<std::uint64_t>(Size) > KMaximumDirblockAggregateBytes - AggregateBytes)
                {
                    if (Error)
                        *Error = "READ DIRBLOCK exceeded its portable content limit";
                    return false;
                }
                AggregateBytes += static_cast<std::uint64_t>(Size);
                std::vector<std::uint8_t> Bytes;
                if (!Context.Kernel->FileIO().FileReadBytes(Bytes, FileNumber, static_cast<std::size_t>(Size), Error))
                    return false;
                const fs::path CanonicalParent = fs::weakly_canonical(Output.parent_path(), FileError);
                if (FileError || !IsConfinedTo(Destination, CanonicalParent))
                {
                    if (Error)
                        *Error = "READ DIRBLOCK entry resolves outside its destination";
                    return false;
                }
                FileError.clear();
                fs::create_directories(Output.parent_path(), FileError);
                if (FileError)
                {
                    if (Error)
                        *Error = "READ DIRBLOCK cannot create parent directories";
                    return false;
                }
                const fs::file_status Existing = fs::symlink_status(Output, FileError);
                if (!FileError && fs::is_symlink(Existing))
                {
                    if (Error)
                        *Error = "READ DIRBLOCK refuses to replace a symbolic link";
                    return false;
                }
                FileError.clear();
                std::ofstream Stream(Output, std::ios::binary | std::ios::trunc);
                if (!Stream || (!Bytes.empty() && !Stream.write(reinterpret_cast<const char*>(Bytes.data()),
                                                                static_cast<std::streamsize>(Bytes.size()))))
                {
                    if (Error)
                        *Error = "READ DIRBLOCK cannot write '" + dbc::PathToUtf8(Output) + "'";
                    return false;
                }
            }
            return true;
        }

        struct CommandEntry
        {
            using Handler = bool (*)(BuiltinContext&, const std::vector<BuiltinValue>&, int, std::string&);

            CommandEntry(int, int, Handler Value) : Execute(Value) {}

            Handler Execute = nullptr;
        };

        struct FunctionEntry
        {
            using Handler = BuiltinValue (*)(BuiltinContext&, const std::vector<BuiltinValue>&, int, std::string&);

            FunctionEntry(int, int, Handler Value) : Execute(Value) {}

            Handler Execute = nullptr;
        };

        bool CheckInvocation(const language::SymbolDescriptor& Descriptor, language::LanguageProfile Profile,
                             std::size_t Actual, std::string& Error)
        {
            const bool IsCommand = Descriptor.Kind == language::SymbolKind::Command;
            if (!language::AvailableIn(Descriptor, Profile))
            {
                Error = std::string(IsCommand ? "Command is not available in this profile: "
                                              : "Function is not available in this profile: ") +
                        std::string(Descriptor.CanonicalName);
                return false;
            }
            if (language::SupportFor(Descriptor, Profile) != language::SupportStatus::Implemented)
            {
                Error = std::string(IsCommand ? "Unsupported command: " : "Unsupported function: ") +
                        std::string(Descriptor.CanonicalName);
                return false;
            }
            const std::optional<language::Arity> Arity = language::ArityFor(Descriptor, Profile);
            if (!Arity)
            {
                Error = "Missing catalogue signature: " + std::string(Descriptor.CanonicalName);
                return false;
            }

            const int Count = static_cast<int>(Actual);
            if (!Arity->Accepts(Count))
            {
                Error = std::string(Descriptor.CanonicalName) + " expects ";
                if (Arity->AcceptedCounts != 0)
                {
                    bool First = true;
                    for (int Candidate = 0; Candidate < 64; ++Candidate)
                    {
                        if ((Arity->AcceptedCounts & (std::uint64_t{1} << Candidate)) == 0)
                            continue;
                        if (!First)
                            Error += " or ";
                        Error += std::to_string(Candidate);
                        First = false;
                    }
                }
                else
                {
                    Error += std::to_string(Arity->Min);
                    if (Arity->Min != Arity->Max)
                        Error += ".." + std::to_string(Arity->Max);
                }
                Error += " argument(s), got " + std::to_string(Count);
                return false;
            }
            return true;
        }

        const language::SymbolDescriptor& DescriptorForBinding(language::SymbolKind Kind, std::string_view Name)
        {
            const language::SymbolDescriptor* Result = nullptr;
            for (const language::SymbolDescriptor& Descriptor : language::Symbols())
            {
                if (Descriptor.Kind != Kind || Descriptor.CanonicalName != Name)
                    continue;
                assert(!Result && "Duplicate catalogue binding");
                Result = &Descriptor;
            }
            assert(Result && "Engine handler has no catalogue symbol");
            return *Result;
        }

        double SystemMemoryBytes()
        {
            const int RamMb = SDL_GetSystemRAM();
            return RamMb > 0 ? static_cast<double>(RamMb) * 1024.0 * 1024.0 : 0.0;
        }

        std::string GraphicsCardName(BuiltinContext& Ctx)
        {
            if (Ctx.Kernel && Ctx.Kernel->Gpu().GpuDevice())
            {
                const SDL_PropertiesID Properties = SDL_GetGPUDeviceProperties(Ctx.Kernel->Gpu().GpuDevice());
                if (const char* Name = SDL_GetStringProperty(Properties, SDL_PROP_GPU_DEVICE_NAME_STRING, nullptr);
                    Name && *Name)
                {
                    return Name;
                }
                if (const char* Driver = SDL_GetGPUDeviceDriver(Ctx.Kernel->Gpu().GpuDevice()))
                {
                    return Driver;
                }
            }
            return "SDL_GPU";
        }

        std::pair<int, int> DesktopSize(BuiltinContext& Ctx)
        {
            SDL_Window* Window = Ctx.Kernel ? Ctx.Kernel->Core().Window() : nullptr;
            const SDL_DisplayID Display = Window ? SDL_GetDisplayForWindow(Window) : SDL_GetPrimaryDisplay();
            const SDL_DisplayMode* Desktop = Display ? SDL_GetDesktopDisplayMode(Display) : nullptr;
            return Desktop ? std::pair<int, int>{Desktop->w, Desktop->h} : std::pair<int, int>{0, 0};
        }

        double CheckDisplayMode(BuiltinContext& Ctx, int Width, int Height, int Depth, int Vsync, int Multisampling,
                                int Multimonitor)
        {
            if (Width <= 0 || Height <= 0)
                return 0.0;
            if (Depth != 16 && Depth != 32)
                return 0.0;
            if ((Vsync != 0 && Vsync != 1) || Multisampling != 0 || Multimonitor != 0)
                return 0.0;
            SDL_Window* Window = Ctx.Kernel ? Ctx.Kernel->Core().Window() : nullptr;
            const SDL_DisplayID Display = Window ? SDL_GetDisplayForWindow(Window) : SDL_GetPrimaryDisplay();
            int Count = 0;
            SDL_DisplayMode** Modes = Display ? SDL_GetFullscreenDisplayModes(Display, &Count) : nullptr;
            bool Found = false;
            for (int Index = 0; Modes && Index < Count; ++Index)
            {
                const SDL_DisplayMode* Mode = Modes[Index];
                if (Mode->w == Width && Mode->h == Height && SDL_BITSPERPIXEL(Mode->format) == Depth)
                {
                    Found = true;
                    break;
                }
            }
            SDL_free(Modes);
            if (Found)
                return 1.0;
            const auto [DesktopWidth, DesktopHeight] = DesktopSize(Ctx);
            return (Width <= DesktopWidth && Height <= DesktopHeight) ? 1.0 : 0.0;
        }

        double PortableShaderVersion(BuiltinContext& Ctx)
        {
            SDL_GPUDevice* Device = Ctx.Kernel ? Ctx.Kernel->Gpu().GpuDevice() : nullptr;
            return Device && SDL_GetGPUShaderFormats(Device) != SDL_GPU_SHADERFORMAT_INVALID ? 3.0 : 0.0;
        }

        double Statistic(BuiltinContext& Ctx, int Code)
        {
            switch (Code)
            {
                case 1:
                    return static_cast<double>(Ctx.Kernel->Renderer().LastFramePolygons());
                case 2:
                    return Ctx.Kernel->Gpu().SupportsStencil() ? 8.0 : 0.0;
                case 5:
                    return static_cast<double>(Ctx.Kernel->Renderer().LastFrameDrawCalls());
                default:
                    return 0.0;
            }
        }

        template <typename T, typename Reader> BuiltinValue ReadBuiltinNumber(Reader&& Read)
        {
            T Value{};
            if (!Read(Value))
                return {};
            return BuiltinValue::FromNumber(static_cast<double>(Value));
        }

        BuiltinValue EntryValue(BuiltinContext& Context, int Mode, std::string& Error)
        {
            std::string Text;
            if (!Context.Kernel->Input().Entry(Text, Mode, &Error))
                return {};
            return BuiltinValue::FromString(std::move(Text));
        }

        const ankerl::unordered_dense::map<std::string, CommandEntry>& Commands()
        {
            static const ankerl::unordered_dense::map<std::string, CommandEntry> KCommands = []
            {
                ankerl::unordered_dense::
                    map<std::string, CommandEntry>
                        Table =
                            {

                // clang-format off
#define DBC_VOID(name, lo, hi, ...)                                                                             \
    {name, {lo, hi, [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int, std::string& Error) -> bool {        \
        (void)Ctx; (void)Args; (void)Error; __VA_ARGS__; return true; }}},
#define DBC_VALUE(name, lo, hi, ...)
#include "engine_builtin_handlers.inc"
#undef DBC_VOID
#undef DBC_VALUE
                                // clang-format on

                                {"LOAD BITMAP",
                                 {1, 2,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      const int BitmapId = (Args.size() >= 2) ? IntegerArgument0 : 0;
                                      const std::string Path = ResolveAssetPath(Ctx, Args[0].AsString());
                                      return Ctx.Kernel->ImageStoreRef().LoadBitmapFile(Path, BitmapId, &Error);
                                  }}},
                                {"LOAD IMAGE",
                                 {2, 4,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 2 && !Args[2].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      int IntegerArgument1 = 0;
                                      if (Args.size() > 3 && !Args[3].AsInt(IntegerArgument1, &Error))
                                          return false;
                                      int IntegerArgument2 = 0;
                                      if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument2, &Error))
                                          return false;
                                      const std::string Path = ResolveAssetPath(Ctx, Args[0].AsString());
                                      const int TextureFlag = Args.size() >= 3 ? IntegerArgument0 : 0;
                                      const int DivideTextureSize = Args.size() >= 4 ? IntegerArgument1 : 0;
                                      return Ctx.Kernel->ImageStoreRef().LoadImageFile(
                                          Path, IntegerArgument2, TextureFlag, DivideTextureSize, &Error);
                                  }}},
                                {"LOAD OBJECT",
                                 {2, 4,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 2 && !Args[2].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      int IntegerArgument1 = 0;
                                      if (Args.size() > 3 && !Args[3].AsInt(IntegerArgument1, &Error))
                                          return false;
                                      int IntegerArgument2 = 0;
                                      if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument2, &Error))
                                          return false;
                                      (void)Error;
                                      const std::string Path = ResolveAssetPath(Ctx, Args[0].AsString());
                                      const int LoadMode = Args.size() >= 3 ? IntegerArgument0 : 0;
                                      const int DivideTextureSize = Args.size() >= 4 ? IntegerArgument1 : 0;
                                      {
                                          if (!LoadObject(*Ctx.Kernel, Path, IntegerArgument2, LoadMode,
                                                          DivideTextureSize, &Error))
                                              return false;
                                      }
                                      return true;
                                  }}},
                                {"SET DISPLAY MODE",
                                 {2, 6,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 2 && !Args[2].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      int IntegerArgument1 = 0;
                                      if (Args.size() > 3 && !Args[3].AsInt(IntegerArgument1, &Error))
                                          return false;
                                      int IntegerArgument2 = 0;
                                      if (Args.size() > 4 && !Args[4].AsInt(IntegerArgument2, &Error))
                                          return false;
                                      int IntegerArgument3 = 0;
                                      if (Args.size() > 5 && !Args[5].AsInt(IntegerArgument3, &Error))
                                          return false;
                                      int IntegerArgument4 = 0;
                                      if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument4, &Error))
                                          return false;
                                      int IntegerArgument5 = 0;
                                      if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument5, &Error))
                                          return false;
                                      const int Depth = Args.size() >= 3 ? IntegerArgument0 : 32;
                                      const int VSync = Args.size() >= 4 ? IntegerArgument1 : 1;
                                      const int Multisampling = Args.size() >= 6 ? IntegerArgument2 : 0;
                                      const int Multimonitor = Args.size() >= 6 ? IntegerArgument3 : 0;
                                      return Ctx.Kernel->Core().SetDisplayMode(IntegerArgument4, IntegerArgument5,
                                                                               Depth, VSync, Multisampling,
                                                                               Multimonitor, &Error);
                                  }}},
                                {"COPY FILE",
                                 {2, 2,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      const fs::path Source = ResolveNativeAssetPath(Ctx, Args[0].AsString());
                                      const fs::path Destination = ResolveNativeAssetPath(Ctx, Args[1].AsString());
                                      std::error_code StatusError;
                                      if (!fs::is_regular_file(Source, StatusError))
                                      {
                                          Error = "COPY FILE source is not a readable file: '" +
                                                  dbc::PathToUtf8(Source) + "'";
                                          return false;
                                      }
                                      StatusError.clear();
                                      if (fs::exists(Destination, StatusError))
                                      {
                                          Error =
                                              "COPY FILE refuses to overwrite: '" + dbc::PathToUtf8(Destination) + "'";
                                          return false;
                                      }
                                      std::error_code CopyError;
                                      if (!fs::copy_file(Source, Destination, fs::copy_options::none, CopyError))
                                      {
                                          Error = "COPY FILE failed from '" + dbc::PathToUtf8(Source) + "' to '" +
                                                  dbc::PathToUtf8(Destination) + "': " + CopyError.message();
                                          return false;
                                      }
                                      return true;
                                  }}},
                                {"GET IMAGE",
                                 {5, 6, // The texturing flag pre-creates a texture in the original; every image is
                                        // GPU-resident here.
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      int IntegerArgument1 = 0;
                                      if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument1, &Error))
                                          return false;
                                      int IntegerArgument2 = 0;
                                      if (Args.size() > 2 && !Args[2].AsInt(IntegerArgument2, &Error))
                                          return false;
                                      int IntegerArgument3 = 0;
                                      if (Args.size() > 3 && !Args[3].AsInt(IntegerArgument3, &Error))
                                          return false;
                                      int IntegerArgument4 = 0;
                                      if (Args.size() > 4 && !Args[4].AsInt(IntegerArgument4, &Error))
                                          return false;
                                      (void)Error;
                                      Ctx.Kernel->ImageStoreRef().GetImage(IntegerArgument0, IntegerArgument1,
                                                                           IntegerArgument2, IntegerArgument3,
                                                                           IntegerArgument4);
                                      return true;
                                  }}},
                                {"SET OBJECT",
                                 {4, 8,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      int IntegerArgument1 = 0;
                                      if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument1, &Error))
                                          return false;
                                      int IntegerArgument2 = 0;
                                      if (Args.size() > 2 && !Args[2].AsInt(IntegerArgument2, &Error))
                                          return false;
                                      int IntegerArgument3 = 0;
                                      if (Args.size() > 3 && !Args[3].AsInt(IntegerArgument3, &Error))
                                          return false;
                                      int IntegerArgument4 = 0;
                                      if (Args.size() > 4 && !Args[4].AsInt(IntegerArgument4, &Error))
                                          return false;
                                      int IntegerArgument5 = 0;
                                      if (Args.size() > 5 && !Args[5].AsInt(IntegerArgument5, &Error))
                                          return false;
                                      int IntegerArgument6 = 0;
                                      if (Args.size() > 6 && !Args[6].AsInt(IntegerArgument6, &Error))
                                          return false;
                                      int IntegerArgument7 = 0;
                                      if (Args.size() > 7 && !Args[7].AsInt(IntegerArgument7, &Error))
                                          return false;
                                      (void)Error;
                                      const int Id = IntegerArgument0;
                                      const int Wire = IntegerArgument1;
                                      const int Trans = IntegerArgument2;
                                      const int Cull = IntegerArgument3;
                                      const int Filter = (Args.size() > 4) ? IntegerArgument4 : 1;
                                      const int Light = (Args.size() > 5) ? IntegerArgument5 : 1;
                                      const int Fog = (Args.size() > 6) ? IntegerArgument6 : 1;
                                      const int Ambient = (Args.size() > 7) ? IntegerArgument7 : 1;
                                      {
                                          if (!SetObject(*Ctx.Kernel, Id, Wire, Trans, Cull, Filter, Light, Fog,
                                                         Ambient, &Error))
                                              return false;
                                      }
                                      return true;
                                  }}},
                                {"GHOST OBJECT ON",
                                 {1, 2,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      int IntegerArgument1 = 0;
                                      if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument1, &Error))
                                          return false;
                                      (void)Error;
                                      const int Mode = (Args.size() > 1) ? IntegerArgument0 : 0;
                                      GhostObjectOn(*Ctx.Kernel, IntegerArgument1, Mode);
                                      return true;
                                  }}},
                                {"PLAY OBJECT",
                                 {1, 3,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      int IntegerArgument1 = 0;
                                      if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument1, &Error))
                                          return false;
                                      int IntegerArgument2 = 0;
                                      if (Args.size() > 2 && !Args[2].AsInt(IntegerArgument2, &Error))
                                          return false;
                                      (void)Error;
                                      if (Args.size() == 1)
                                      {
                                          if (!PlayObject(*Ctx.Kernel, IntegerArgument0, &Error))
                                              return false;
                                      }
                                      else if (Args.size() == 2)
                                      {
                                          if (!PlayObject(*Ctx.Kernel, IntegerArgument0, IntegerArgument1,
                                                          TotalObjectFrames(*Ctx.Kernel, IntegerArgument0), &Error))
                                              return false;
                                      }
                                      else
                                      {
                                          if (!PlayObject(*Ctx.Kernel, IntegerArgument0, IntegerArgument1,
                                                          IntegerArgument2, &Error))
                                              return false;
                                      }
                                      return true;
                                  }}},
                                {"LOOP OBJECT",
                                 {1, 3,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      int IntegerArgument1 = 0;
                                      if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument1, &Error))
                                          return false;
                                      int IntegerArgument2 = 0;
                                      if (Args.size() > 2 && !Args[2].AsInt(IntegerArgument2, &Error))
                                          return false;
                                      (void)Error;
                                      if (Args.size() == 1)
                                      {
                                          if (!LoopObject(*Ctx.Kernel, IntegerArgument0, &Error))
                                              return false;
                                      }
                                      else if (Args.size() == 2)
                                      {
                                          if (!LoopObject(*Ctx.Kernel, IntegerArgument0, IntegerArgument1,
                                                          TotalObjectFrames(*Ctx.Kernel, IntegerArgument0), &Error))
                                              return false;
                                      }
                                      else
                                      {
                                          if (!LoopObject(*Ctx.Kernel, IntegerArgument0, IntegerArgument1,
                                                          IntegerArgument2, &Error))
                                              return false;
                                      }
                                      return true;
                                  }}},
                                {"MAKE OBJECT SPHERE",
                                 {2, 4,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      int IntegerArgument1 = 0;
                                      if (Args.size() > 2 && !Args[2].AsInt(IntegerArgument1, &Error))
                                          return false;
                                      int IntegerArgument2 = 0;
                                      if (Args.size() > 3 && !Args[3].AsInt(IntegerArgument2, &Error))
                                          return false;
                                      (void)Error;
                                      const int Id = IntegerArgument0;
                                      float FloatArgument1 = 0.0f;
                                      if (Args.size() > 1 && !Args[1].AsFloat(FloatArgument1, &Error))
                                          return false;

                                      const float Radius = FloatArgument1;
                                      const int Rings = (Args.size() > 2) ? IntegerArgument1 : 12;
                                      const int Segs = (Args.size() > 3) ? IntegerArgument2 : 16;
                                      MakeObjectSphere(*Ctx.Kernel, Id, Radius, Rings, Segs);
                                      return true;
                                  }}},
                                {"SCALE OBJECT TEXTURE",
                                 {3, 4,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() == 4 && !Args[1].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      int IntegerArgument1 = 0;
                                      if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument1, &Error))
                                          return false;
                                      (void)Error;
                                      const int Stage = Args.size() == 4 ? IntegerArgument0 : 0;
                                      const std::size_t Offset = Args.size() == 4 ? 2u : 1u;
                                      {
                                          float FloatArgument1 = 0.0f;
                                          if (!Args[Offset].AsFloat(FloatArgument1, &Error))
                                              return false;
                                          float FloatArgument2 = 0.0f;
                                          if (!Args[Offset + 1].AsFloat(FloatArgument2, &Error))
                                              return false;

                                          if (!Ctx.Kernel->Objects().ScaleObjectTexture(
                                                  IntegerArgument1, Stage, FloatArgument1, FloatArgument2, &Error))
                                              return false;
                                      }
                                      return true;
                                  }}},
                                {"MAKE OBJECT CYLINDER",
                                 {2, 2,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      int IntegerArgument1 = 0;
                                      if (Args.size() > 3 && !Args[3].AsInt(IntegerArgument1, &Error))
                                          return false;
                                      (void)Error;
                                      const int Id = IntegerArgument0;
                                      float FloatArgument1 = 0.0f;
                                      if (Args.size() > 1 && !Args[1].AsFloat(FloatArgument1, &Error))
                                          return false;

                                      const float Size = FloatArgument1;
                                      const float Radius = (Args.size() > 2) ? Size : Size * 0.5f;
                                      float FloatArgument2 = 0.0f;
                                      if (Args.size() > 2 && !Args[2].AsFloat(FloatArgument2, &Error))
                                          return false;

                                      const float Height = (Args.size() > 2) ? FloatArgument2 : Size;
                                      const int Segments = (Args.size() > 3) ? IntegerArgument1 : 30;
                                      MakeObjectCylinder(*Ctx.Kernel, Id, Radius, Height, Segments);
                                      return true;
                                  }}},
                                {"MAKE OBJECT CONE",
                                 {2, 2,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      int IntegerArgument1 = 0;
                                      if (Args.size() > 3 && !Args[3].AsInt(IntegerArgument1, &Error))
                                          return false;
                                      (void)Error;
                                      const int Id = IntegerArgument0;
                                      float FloatArgument1 = 0.0f;
                                      if (Args.size() > 1 && !Args[1].AsFloat(FloatArgument1, &Error))
                                          return false;

                                      const float Size = FloatArgument1;
                                      const float Radius = (Args.size() > 2) ? Size : Size * 0.5f;
                                      float FloatArgument2 = 0.0f;
                                      if (Args.size() > 2 && !Args[2].AsFloat(FloatArgument2, &Error))
                                          return false;

                                      const float Height = (Args.size() > 2) ? FloatArgument2 : Size;
                                      const int Segs = (Args.size() > 3) ? IntegerArgument1 : 16;
                                      MakeObjectCone(*Ctx.Kernel, Id, Radius, Height, Segs);
                                      return true;
                                  }}},
                                {"PRINT",
                                 {0, 99,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      (void)Error;
                                      std::string Text;
                                      if (!Args.empty())
                                      {
                                          for (const auto& A : Args)
                                              Text += A.AsString();
                                      }
                                      Print(*Ctx.Kernel, Text);
                                      Text = std::move(Ctx.PendingPrint) + Text;
                                      Ctx.PendingPrint.clear();
                                      return WriteHostLine(Ctx, Text, &Error);
                                  }}},
                                // File I/O commands
                                {"CLOSE FILE",
                                 {1, 1,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      (void)Error;
                                      if (!Ctx.Kernel->FileIO().CloseFile(IntegerArgument0, &Error))
                                          return false;
                                      return true;
                                  }}},
                                // READ commands: 2-arg form "READ BYTE file_num, varname" — handled as special case in
                                // VM
                                {"MAKE FILE",
                                 {1, 1,
                                  [](
                                      BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args,
                                      int, std::string& Error)
                                  {
                                      (void)Error;
                                      const fs::path Path = ResolveNativeAssetPath(Ctx, Args[0].AsString());
                                      std::ofstream Output(Path, std::ios::binary | std::ios::trunc);
                                      if (!Output)
                                      {
                                          Error = "MAKE FILE failed for '" + dbc::PathToUtf8(Path) + "'";
                                          return false;
                                      }
                                      return true;
                                  }}},
                                {"DELETE FILE",
                                 {1, 1,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      (void)Error;
                                      const fs::path Path = ResolveNativeAssetPath(Ctx, Args[0].AsString());
                                      std::error_code FileError;
                                      if (!fs::is_regular_file(Path, FileError) || !fs::remove(Path, FileError))
                                      {
                                          Error = "DELETE FILE failed for '" + dbc::PathToUtf8(Path) + "'";
                                          return false;
                                      }
                                      return true;
                                  }}},
                                {"MAKE DIRECTORY",
                                 {1, 1,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      (void)Error;
                                      const fs::path Path = ResolveNativeAssetPath(Ctx, Args[0].AsString());
                                      std::error_code FileError;
                                      if (!fs::create_directory(Path, FileError))
                                      {
                                          Error = "MAKE DIRECTORY failed for '" + dbc::PathToUtf8(Path) + "'";
                                          return false;
                                      }
                                      return true;
                                  }}},
                                {"DELETE DIRECTORY",
                                 {1, 1,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      (void)Error;
                                      const fs::path Path = ResolveNativeAssetPath(Ctx, Args[0].AsString());
                                      std::error_code FileError;
                                      const fs::file_status Status = fs::symlink_status(Path, FileError);
                                      if (FileError || fs::is_symlink(Status))
                                      {
                                          Error = "DELETE DIRECTORY refuses a symbolic link: '" +
                                                  dbc::PathToUtf8(Path) + "'";
                                          return false;
                                      }
                                      const fs::path Canonical = fs::weakly_canonical(Path, FileError);
                                      std::error_code ActiveError;
                                      const fs::path Active = fs::weakly_canonical(Ctx.AssetRoot, ActiveError);
                                      if (FileError || !fs::is_directory(Canonical, FileError) ||
                                          Canonical == Canonical.root_path() || (!ActiveError && Canonical == Active))
                                      {
                                          Error = "DELETE DIRECTORY refuses an invalid or active directory: '" +
                                                  dbc::PathToUtf8(Path) + "'";
                                          return false;
                                      }
                                      FileError.clear();
                                      if (fs::remove_all(Canonical, FileError) == 0 || FileError)
                                      {
                                          Error = "DELETE DIRECTORY failed for '" + dbc::PathToUtf8(Path) + "'";
                                          return false;
                                      }
                                      return true;
                                  }}},
                                {"MOVE FILE",
                                 {2, 2,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      (void)Error;
                                      if (!RenameFile(Ctx, Args, "MOVE FILE", &Error))
                                          return false;
                                      return true;
                                  }}},
                                {"RENAME FILE",
                                 {2, 2,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      (void)Error;
                                      if (!RenameFile(Ctx, Args, "RENAME FILE", &Error))
                                          return false;
                                      return true;
                                  }}},
                                {"SET DIR",
                                 {1, 1,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      (void)Error;
                                      if (!RequireDirectory(Ctx.AssetRoot, Ctx, Args[0].AsString(), "SET DIR", &Error))
                                          return false;
                                      return true;
                                  }}},
                                {"FIND FIRST",
                                 {0, 0,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>&, int, std::string& Error)
                                  {
                                      (void)Error;
                                      if (!Ctx.Kernel->FileIO().FindFirst(dbc::PathToUtf8(Ctx.AssetRoot), &Error))
                                          return false;
                                      return true;
                                  }}},
                                {"FIND NEXT",
                                 {0, 0,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>&, int, std::string& Error)
                                  {
                                      (void)Error;
                                      if (!Ctx.Kernel->FileIO().FindNext(&Error))
                                          return false;
                                      return true;
                                  }}},
                                // Display and bitmap commands
                                {"COPY BITMAP",
                                 {2, 10,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      int IntegerArgument1 = 0;
                                      if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument1, &Error))
                                          return false;
                                      int IntegerArgument2 = 0;
                                      if (Args.size() > 2 && !Args[2].AsInt(IntegerArgument2, &Error))
                                          return false;
                                      int IntegerArgument3 = 0;
                                      if (Args.size() > 3 && !Args[3].AsInt(IntegerArgument3, &Error))
                                          return false;
                                      int IntegerArgument4 = 0;
                                      if (Args.size() > 4 && !Args[4].AsInt(IntegerArgument4, &Error))
                                          return false;
                                      int IntegerArgument5 = 0;
                                      if (Args.size() > 5 && !Args[5].AsInt(IntegerArgument5, &Error))
                                          return false;
                                      int IntegerArgument6 = 0;
                                      if (Args.size() > 6 && !Args[6].AsInt(IntegerArgument6, &Error))
                                          return false;
                                      int IntegerArgument7 = 0;
                                      if (Args.size() > 7 && !Args[7].AsInt(IntegerArgument7, &Error))
                                          return false;
                                      int IntegerArgument8 = 0;
                                      if (Args.size() > 8 && !Args[8].AsInt(IntegerArgument8, &Error))
                                          return false;
                                      int IntegerArgument9 = 0;
                                      if (Args.size() > 9 && !Args[9].AsInt(IntegerArgument9, &Error))
                                          return false;
                                      (void)Error;
                                      if (Args.size() <= 2)
                                          return Ctx.Kernel->ImageStoreRef().CopyBitmap(IntegerArgument0,
                                                                                        IntegerArgument1, &Error);
                                      else if (Args.size() >= 10)
                                          return Ctx.Kernel->ImageStoreRef().CopyBitmapRegion(
                                              IntegerArgument0, IntegerArgument1, IntegerArgument2, IntegerArgument3,
                                              IntegerArgument4, IntegerArgument5, IntegerArgument6, IntegerArgument7,
                                              IntegerArgument8, IntegerArgument9, &Error);
                                      return true;
                                  }}},
                                {"FADE BITMAP",
                                 {2, 2,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      int IntegerArgument1 = 0;
                                      if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument1, &Error))
                                          return false;
                                      (void)Error;
                                      return Ctx.Kernel->ImageStoreRef().FadeBitmap(IntegerArgument0, IntegerArgument1,
                                                                                    &Error);
                                      return true;
                                  }}},
                                // Always-active commands

                                // Memblock commands
                                {"WRITE MEMBLOCK WORD",
                                 {3, 3,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      int IntegerArgument1 = 0;
                                      if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument1, &Error))
                                          return false;
                                      int IntegerArgument2 = 0;
                                      if (Args.size() > 2 && !Args[2].AsInt(IntegerArgument2, &Error))
                                          return false;
                                      (void)Error;
                                      if (!WriteMemblockWord(*Ctx.Kernel, IntegerArgument0, IntegerArgument1,
                                                             IntegerArgument2, &Error))
                                          return false;
                                      return true;
                                  }}},
                                {"COPY MEMBLOCK",
                                 {5, 5,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      int IntegerArgument1 = 0;
                                      if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument1, &Error))
                                          return false;
                                      int IntegerArgument2 = 0;
                                      if (Args.size() > 2 && !Args[2].AsInt(IntegerArgument2, &Error))
                                          return false;
                                      int IntegerArgument3 = 0;
                                      if (Args.size() > 3 && !Args[3].AsInt(IntegerArgument3, &Error))
                                          return false;
                                      int IntegerArgument4 = 0;
                                      if (Args.size() > 4 && !Args[4].AsInt(IntegerArgument4, &Error))
                                          return false;
                                      (void)Error;
                                      if (!Ctx.Kernel->Memblocks().CopyMemblock(IntegerArgument0, IntegerArgument1,
                                                                                IntegerArgument2, IntegerArgument3,
                                                                                IntegerArgument4, &Error))
                                          return false;
                                      return true;
                                  }}},
                                {"READ MEMBLOCK",
                                 {2, 2,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      int IntegerArgument1 = 0;
                                      if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument1, &Error))
                                          return false;
                                      (void)Error;
                                      int Size{};
                                      if (!Ctx.Kernel->FileIO().FileReadLong(Size, IntegerArgument0, &Error))
                                          return false;
                                      if (Size <= 0)
                                      {
                                          Error = "READ MEMBLOCK found an invalid size header";
                                          return false;
                                      }
                                      std::vector<uint8_t> Bytes;
                                      if (!Ctx.Kernel->FileIO().FileReadBytes(Bytes, IntegerArgument0,
                                                                              static_cast<std::size_t>(Size), &Error))
                                          return false;
                                      if (!Ctx.Kernel->Memblocks().ReplaceMemblock(IntegerArgument1, std::move(Bytes),
                                                                                   &Error))
                                          return false;
                                      return true;
                                  }}},
                                {"WRITE MEMBLOCK",
                                 {2, 2,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      int IntegerArgument1 = 0;
                                      if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument1, &Error))
                                          return false;
                                      (void)Error;
                                      const auto* Data = Ctx.Kernel->Memblocks().MemblockData(IntegerArgument0, &Error);
                                      if (!Data)
                                          return false;
                                      const auto& Bytes = *Data;
                                      if (Bytes.size() >
                                          static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
                                      {
                                          Error = "WRITE MEMBLOCK size exceeds the historical 32-bit format";
                                          return false;
                                      }
                                      if (!Ctx.Kernel->FileIO().FileWriteLong(IntegerArgument1,
                                                                              static_cast<int>(Bytes.size()), &Error))
                                          return false;
                                      if (!Ctx.Kernel->FileIO().FileWriteBytes(IntegerArgument1, Bytes, &Error))
                                          return false;
                                      return true;
                                  }}},
                                {"MAKE MEMBLOCK FROM FILE",
                                 {2, 2,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      int IntegerArgument1 = 0;
                                      if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument1, &Error))
                                          return false;
                                      (void)Error;
                                      std::size_t Size{};
                                      if (!Ctx.Kernel->FileIO().FileSize(Size, IntegerArgument0, &Error))
                                          return false;
                                      if (Size == 0)
                                      {
                                          Error = "MAKE MEMBLOCK FROM FILE cannot read an empty file";
                                          return false;
                                      }
                                      std::vector<uint8_t> Bytes;
                                      if (!Ctx.Kernel->FileIO().FileReadBytes(Bytes, IntegerArgument0, Size, &Error))
                                          return false;
                                      if (!Ctx.Kernel->Memblocks().ReplaceMemblock(IntegerArgument1, std::move(Bytes),
                                                                                   &Error))
                                          return false;
                                      return true;
                                  }}},
                                {"MAKE FILE FROM MEMBLOCK",
                                 {2, 2,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      int IntegerArgument1 = 0;
                                      if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument1, &Error))
                                          return false;
                                      (void)Error;
                                      const auto* Data = Ctx.Kernel->Memblocks().MemblockData(IntegerArgument0, &Error);
                                      if (!Data)
                                          return false;
                                      if (!Ctx.Kernel->FileIO().FileWriteBytes(IntegerArgument1, *Data, &Error))
                                          return false;
                                      return true;
                                  }}},
                                {"SKIP BYTES",
                                 {2, 2,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      int IntegerArgument1 = 0;
                                      if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument1, &Error))
                                          return false;
                                      (void)Error;
                                      if (!Ctx.Kernel->FileIO().SkipBytes(IntegerArgument0, IntegerArgument1, &Error))
                                          return false;
                                      return true;
                                  }}},
                                {"WRITE BYTE TO FILE",
                                 {3, 3,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      int IntegerArgument1 = 0;
                                      if (Args.size() > 2 && !Args[2].AsInt(IntegerArgument1, &Error))
                                          return false;
                                      (void)Error;
                                      const fs::path Path = ResolveNativeAssetPath(Ctx, Args[0].AsString());
                                      const int Position = IntegerArgument0;
                                      std::error_code FileError;
                                      const std::uintmax_t Size = fs::file_size(Path, FileError);
                                      if (FileError || Position < 0 || static_cast<std::uintmax_t>(Position) >= Size)
                                      {
                                          Error =
                                              "WRITE BYTE TO FILE position is outside '" + dbc::PathToUtf8(Path) + "'";
                                          return false;
                                      }
                                      std::fstream File(Path, std::ios::in | std::ios::out | std::ios::binary);
                                      File.seekp(Position);
                                      const char Byte = static_cast<char>(IntegerArgument1);
                                      File.write(&Byte, 1);
                                      if (!File)
                                      {
                                          Error = "WRITE BYTE TO FILE failed for '" + dbc::PathToUtf8(Path) + "'";
                                          return false;
                                      }
                                      return true;
                                  }}},
                                // 2D drawing
                                // BOB commands
                                // Sprite extras
                                // BOB commands
                                // System / window commands
                                {"SAVE BITMAP",
                                 {1, 2,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      (void)Error;
                                      const int Bmp = (Args.size() > 1) ? IntegerArgument0 : 0;
                                      SaveBitmap(*Ctx.Kernel, ResolveAssetPath(Ctx, Args[0].AsString()), Bmp);
                                      return true;
                                  }}},
                                // --- DBPro VECTOR2 / VECTOR3 / VECTOR4 ---------------------------
                                // Maths in dba_vecmath.hpp, written once per operation and
                                // instantiated for each width.
                                // --- DBPro MATRIX4 -------------------------------------------------
                                // Pure maths uses the DBPro/DirectX row-vector layout from
                                // dba_matrix4.hpp. Camera matrices derive from the same state
                                // as the renderer.
                                // Two forms: (result, a, b) concatenates, (result, value) scales.
                                {"MULTIPLY MATRIX4",
                                 {2, 3,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      (void)Error;
                                      {
                                          if (!MultiplyMatrix4(Ctx, Args, &Error))
                                              return false;
                                      }
                                      return true;
                                  }}},
                                {"SET VECTOR3",
                                 {4, 4,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      (void)Error;
                                      {
                                          float FloatArgument1 = 0.0f;
                                          if (Args.size() > 1 && !Args[1].AsFloat(FloatArgument1, &Error))
                                              return false;
                                          float FloatArgument2 = 0.0f;
                                          if (Args.size() > 2 && !Args[2].AsFloat(FloatArgument2, &Error))
                                              return false;
                                          float FloatArgument3 = 0.0f;
                                          if (Args.size() > 3 && !Args[3].AsFloat(FloatArgument3, &Error))
                                              return false;

                                          if (!StoreVector(Ctx.Vectors, IntegerArgument0,
                                                           VecN<3>{FloatArgument1, FloatArgument2, FloatArgument3},
                                                           "SET VECTOR3", &Error))
                                              return false;
                                      }
                                      return true;
                                  }}},
                                {"ADD VECTOR3",
                                 {3, 3,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      (void)Error;
                                      {
                                          if (!AddVector(Ctx.Vectors, Args, "ADD VECTOR3", &Error))
                                              return false;
                                      }
                                      return true;
                                  }}},
                                {"SUBTRACT VECTOR3",
                                 {3, 3,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      (void)Error;
                                      {
                                          if (!SubtractVector(Ctx.Vectors, Args, "SUBTRACT VECTOR3", &Error))
                                              return false;
                                      }
                                      return true;
                                  }}},
                                {"MULTIPLY VECTOR3",
                                 {2, 2,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      (void)Error;
                                      {
                                          if (!MultiplyVector(Ctx.Vectors, Args, "MULTIPLY VECTOR3", &Error))
                                              return false;
                                      }
                                      return true;
                                  }}},
                                {"SCALE VECTOR3",
                                 {3, 3,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      (void)Error;
                                      {
                                          if (!ScaleVector(Ctx.Vectors, Args, "SCALE VECTOR3", &Error))
                                              return false;
                                      }
                                      return true;
                                  }}},
                                {"CROSS PRODUCT VECTOR3",
                                 {3, 3,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      int IntegerArgument0 = 0;
                                      if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument0, &Error))
                                          return false;
                                      int IntegerArgument1 = 0;
                                      if (Args.size() > 2 && !Args[2].AsInt(IntegerArgument1, &Error))
                                          return false;
                                      int IntegerArgument2 = 0;
                                      if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument2, &Error))
                                          return false;
                                      (void)Error;
                                      auto* CheckedMathValue1 =
                                          RequireVector(Ctx.Vectors, IntegerArgument0, "CROSS PRODUCT VECTOR3", &Error);
                                      if (!CheckedMathValue1)
                                          return false;

                                      const VecN<3> A = (*CheckedMathValue1);
                                      auto* CheckedMathValue2 =
                                          RequireVector(Ctx.Vectors, IntegerArgument1, "CROSS PRODUCT VECTOR3", &Error);
                                      if (!CheckedMathValue2)
                                          return false;

                                      const VecN<3> B = (*CheckedMathValue2);
                                      {
                                          if (!StoreVector(Ctx.Vectors, IntegerArgument2,
                                                           VecN<3>{A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2],
                                                                   A[0] * B[1] - A[1] * B[0]},
                                                           "CROSS PRODUCT VECTOR3", &Error))
                                              return false;
                                      }
                                      return true;
                                  }}},
                                {"NORMALIZE VECTOR3",
                                 {2, 2,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      (void)Error;
                                      {
                                          if (!NormalizeVector(Ctx.Vectors, Args, "NORMALIZE VECTOR3", &Error))
                                              return false;
                                      }
                                      return true;
                                  }}},
                                // Object movement
                                // Matrix normals
                                // Static collision commands
                                // File/directory extras
                                {"EMPTY CHECKLIST",
                                 {0, 0,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>&, int, std::string& Error)
                                  {
                                      (void)Error;
                                      EmptyChecklist(Ctx);
                                      return true;
                                  }}},
                                // Checklist variants
                                {"PERFORM CHECKLIST FOR FILES",
                                 {0, 1,
                                  [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int,
                                     std::string& Error)
                                  {
                                      (void)Error;
                                      EmptyChecklist(Ctx);
                                      std::error_code Ec;
                                      const fs::path Dir = ResolveNativeAssetPath(
                                          Ctx, Args.empty() ? std::string(".") : Args[0].AsString());
                                      std::vector<fs::directory_entry> Entries;
                                      for (fs::directory_iterator It(Dir, Ec), End; !Ec && It != End; It.increment(Ec))
                                          Entries.push_back(*It);
                                      if (Ec)
                                      {
                                          Error = "PERFORM CHECKLIST FOR FILES failed for '" + dbc::PathToUtf8(Dir) +
                                                  "': " + Ec.message();
                                          return false;
                                      }
                                      std::sort(Entries.begin(), Entries.end(),
                                                [](const auto& Left, const auto& Right)
                                                {
                                                    return dbc::PathToUtf8(Left.path().filename()) <
                                                           dbc::PathToUtf8(Right.path().filename());
                                                });
                                      for (const auto& Entry : Entries)
                                      {
                                          ChecklistItem Item;
                                          Item.Name = dbc::PathToUtf8(Entry.path().filename());
                                          std::error_code EntryError;
                                          const auto Status = Entry.status(EntryError);
                                          if (EntryError)
                                          {
                                              Error = "Could not inspect checklist entry: " + EntryError.message();
                                              return false;
                                          }
                                          Item.A = fs::is_directory(Status) ? 1 : 0;
                                          if (fs::is_regular_file(Status))
                                          {
                                              const auto Size = Entry.file_size(EntryError);
                                              if (EntryError ||
                                                  Size > static_cast<std::uintmax_t>(std::numeric_limits<int>::max()))
                                              {
                                                  Error = "Checklist file size is unreadable or outside the BASIC "
                                                          "integer range";
                                                  return false;
                                              }
                                              Item.B = static_cast<int>(Size);
                                          }
                                          Ctx.Checklist.push_back(std::move(Item));
                                      }
                                      return true;
                                  }}},
                            };
                return Table;
            }();
            return KCommands;
        }

        const ankerl::unordered_dense::map<std::string, FunctionEntry>& Functions()
        {
            static const ankerl::unordered_dense::map<std::string, FunctionEntry> KFunctions = []
            {
                ankerl::unordered_dense::map<std::string, FunctionEntry>
                    Table =
                        {

                // clang-format off
#define DBC_VOID(name, lo, hi, ...)
#define DBC_VALUE(name, lo, hi, ...)                                                                            \
    {name, {lo, hi, [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int, std::string& Error) -> BuiltinValue {       \
        (void)Ctx; (void)Args; (void)Error; __VA_ARGS__; }}},
#include "engine_builtin_handlers.inc"
#undef DBC_VOID
#undef DBC_VALUE
                            // clang-format on

                            {"SCANCODE",
                             {0, 0,
                              [](BuiltinContext& Ctx, const std::vector<BuiltinValue>&, int, std::string&)
                              {
                                  return BuiltinValue::FromNumber(static_cast<double>(Ctx.Kernel->Input().Scancode()));
                              }}},
                            {"MID$",
                             {2, 3,
                              [](BuiltinContext&, const std::vector<BuiltinValue>& Args, int, std::string& Error)
                              {
                                  int IntegerArgument0 = 0;
                                  if (Args.size() > 2 && !Args[2].AsInt(IntegerArgument0, &Error))
                                      return BuiltinValue{};
                                  int IntegerArgument1 = 0;
                                  if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument1, &Error))
                                      return BuiltinValue{};
                                  const int Count = (Args.size() > 2) ? IntegerArgument0 : 1;
                                  return BuiltinValue::FromString(Mid(Args[0].AsString(), IntegerArgument1, Count));
                              }}},
                            {"STR$",
                             {1, 2,
                              [](BuiltinContext&, const std::vector<BuiltinValue>& Args, int, std::string& Error)
                              {
                                  int IntegerArgument0 = 0;
                                  if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument0, &Error))
                                      return BuiltinValue{};
                                  if (Args.size() == 1 || Args[0].IsString)
                                      return BuiltinValue::FromString(Args[0].AsString());
                                  const int DecimalPlaces = std::clamp(IntegerArgument0, 0, 18);
                                  std::ostringstream Stream;
                                  Stream << std::fixed << std::setprecision(DecimalPlaces) << Args[0].AsNumber();
                                  return BuiltinValue::FromString(Stream.str());
                              }}},
                            {"FILE EXIST",
                             {1, 1,
                              [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int, std::string&)
                              {
                                  namespace fs = std::filesystem;
                                  std::error_code Ec;
                                  const bool Exists =
                                      fs::is_regular_file(ResolveNativeAssetPath(Ctx, Args[0].AsString()), Ec);
                                  return BuiltinValue::FromNumber(Exists ? 1.0 : 0.0);
                              }}},
                            {"RGBB",
                             {1, 1,
                              [](BuiltinContext&, const std::vector<BuiltinValue>& Args, int, std::string& Error)
                              {
                                  int IntegerArgument0 = 0;
                                  if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                      return BuiltinValue{};
                                  return BuiltinValue::FromNumber(static_cast<double>(IntegerArgument0 & 0xFF));
                              }}},
                            // Audio query functions
                            // System date/time
                            // Sprite collision
                            // Math extensions
                            // Memory
                            {"MEMORY FREE",
                             {0, 0, [](BuiltinContext&, const std::vector<BuiltinValue>&, int, std::string&)
                              { return BuiltinValue::FromNumber(SystemMemoryBytes()); }}},
                            // File I/O read functions (function form: x = read byte(file_num))
                            {"READ BYTE",
                             {1, 1,
                              [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int, std::string& Error)
                              {
                                  int IntegerArgument0 = 0;
                                  if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                      return BuiltinValue{};
                                  return ReadBuiltinNumber<int>(
                                      [&](int& Output)
                                      { return Ctx.Kernel->FileIO().FileReadByte(Output, IntegerArgument0, &Error); });
                              }}},
                            {"READ WORD",
                             {1, 1,
                              [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int, std::string& Error)
                              {
                                  int IntegerArgument0 = 0;
                                  if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                      return BuiltinValue{};
                                  return ReadBuiltinNumber<int>(
                                      [&](int& Output)
                                      { return Ctx.Kernel->FileIO().FileReadWord(Output, IntegerArgument0, &Error); });
                              }}},
                            {"READ LONG",
                             {1, 1,
                              [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int, std::string& Error)
                              {
                                  int IntegerArgument0 = 0;
                                  if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                      return BuiltinValue{};
                                  return ReadBuiltinNumber<int>(
                                      [&](int& Output)
                                      { return Ctx.Kernel->FileIO().FileReadLong(Output, IntegerArgument0, &Error); });
                              }}},
                            {"READ FLOAT",
                             {1, 1,
                              [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int, std::string& Error)
                              {
                                  int IntegerArgument0 = 0;
                                  if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                      return BuiltinValue{};
                                  return ReadBuiltinNumber<float>(
                                      [&](float& Output)
                                      { return Ctx.Kernel->FileIO().FileReadFloat(Output, IntegerArgument0, &Error); });
                              }}},
                            // Memblock queries
                            // Memblock aliases (MEMBLOCK BYTE/WORD/DWORD/FLOAT without "READ" prefix)
                            {"MEMBLOCK FLOAT",
                             {2, 2,
                              [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int, std::string& Error)
                              {
                                  int IntegerArgument0 = 0;
                                  if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                      return BuiltinValue{};
                                  int IntegerArgument1 = 0;
                                  if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument1, &Error))
                                      return BuiltinValue{};
                                  return ReadBuiltinNumber<float>(
                                      [&](float& Output)
                                      {
                                          return ReadMemblockFloat(*Ctx.Kernel, Output, IntegerArgument0,
                                                                   IntegerArgument1, &Error);
                                      });
                              }}},
                            // Collision
                            // Keyboard entry buffer
                            // Sprite query functions
                            // BOB query functions
                            // Hyperbolic trig
                            // Text queries
                            {"TEXT FONT$",
                             {0, 0, [](BuiltinContext& Ctx, const std::vector<BuiltinValue>&, int, std::string&)
                              { return BuiltinValue::FromString(TextFont(*Ctx.Kernel)); }}},
                            // Checklist
                            // Bitmap queries
                            // Filesystem
                            // Audio queries
                            {"LISTENER ANGLE Z",
                             {0, 0,
                              [](BuiltinContext& Ctx, const std::vector<BuiltinValue>&, int, std::string&)
                              {
                                  return BuiltinValue::FromNumber(
                                      static_cast<double>(Ctx.Kernel->Audio().ListenerAngleZ()));
                              }}},
                            // Matrix queries
                            // Limb queries
                            {"LIMB VISIBLE",
                             {2, 2,
                              [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int, std::string& Error)
                              {
                                  int IntegerArgument0 = 0;
                                  if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                      return BuiltinValue{};
                                  int IntegerArgument1 = 0;
                                  if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument1, &Error))
                                      return BuiltinValue{};
                                  return BuiltinValue::FromNumber(static_cast<double>(
                                      Ctx.Kernel->Objects().LimbVisible(IntegerArgument0, IntegerArgument1)));
                              }}},
                            // Object
                            // Image size queries
                            {"IMAGE HEIGHT",
                             {1, 1,
                              [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int, std::string& Error)
                              {
                                  int IntegerArgument0 = 0;
                                  if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                      return BuiltinValue{};
                                  return BuiltinValue::FromNumber(
                                      static_cast<double>(Ctx.Kernel->ImageStoreRef().ImageHeight(IntegerArgument0)));
                              }}},
                            // Registry and miscellaneous queries
                            {"X VECTOR3",
                             {1, 1,
                              [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int, std::string& Error)
                              {
                                  int IntegerArgument0 = 0;
                                  if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                      return BuiltinValue{};
                                  auto* CheckedMathValue1 =
                                      RequireVector(Ctx.Vectors, IntegerArgument0, "X VECTOR3", &Error);
                                  if (!CheckedMathValue1)
                                      return BuiltinValue{};

                                  return BuiltinValue::FromNumber(static_cast<double>((*CheckedMathValue1)[0]));
                              }}},
                            {"Y VECTOR3",
                             {1, 1,
                              [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int, std::string& Error)
                              {
                                  int IntegerArgument0 = 0;
                                  if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                      return BuiltinValue{};
                                  auto* CheckedMathValue1 =
                                      RequireVector(Ctx.Vectors, IntegerArgument0, "Y VECTOR3", &Error);
                                  if (!CheckedMathValue1)
                                      return BuiltinValue{};

                                  return BuiltinValue::FromNumber(static_cast<double>((*CheckedMathValue1)[1]));
                              }}},
                            {"Z VECTOR3",
                             {1, 1,
                              [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int, std::string& Error)
                              {
                                  int IntegerArgument0 = 0;
                                  if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                      return BuiltinValue{};
                                  auto* CheckedMathValue1 =
                                      RequireVector(Ctx.Vectors, IntegerArgument0, "Z VECTOR3", &Error);
                                  if (!CheckedMathValue1)
                                      return BuiltinValue{};

                                  return BuiltinValue::FromNumber(static_cast<double>((*CheckedMathValue1)[2]));
                              }}},
                            // VECTOR2 / VECTOR3 / VECTOR4 query functions.
                            {"CCW VECTOR2",
                             {2, 2,
                              [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int, std::string& Error)
                              {
                                  int IntegerArgument0 = 0;
                                  if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                      return BuiltinValue{};
                                  int IntegerArgument1 = 0;
                                  if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument1, &Error))
                                      return BuiltinValue{};
                                  auto* CheckedMathValue1 =
                                      RequireVector(Ctx.Vectors2, IntegerArgument0, "CCW VECTOR2", &Error);
                                  if (!CheckedMathValue1)
                                      return BuiltinValue{};
                                  auto* CheckedMathValue2 =
                                      RequireVector(Ctx.Vectors2, IntegerArgument1, "CCW VECTOR2", &Error);
                                  if (!CheckedMathValue2)
                                      return BuiltinValue{};

                                  return BuiltinValue::FromNumber(
                                      static_cast<double>(VecCcw2((*CheckedMathValue1), (*CheckedMathValue2))));
                              }}},
                            // MATRIX4 query functions.
                            {"GET MATRIX4 ELEMENT",
                             {2, 2,
                              [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int, std::string& Error)
                              {
                                  int IntegerArgument0 = 0;
                                  if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                      return BuiltinValue{};
                                  int IntegerArgument1 = 0;
                                  if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument1, &Error))
                                      return BuiltinValue{};
                                  auto* CheckedMathValue1 =
                                      RequireMatrix4(Ctx, IntegerArgument0, "GET MATRIX4 ELEMENT", &Error);
                                  if (!CheckedMathValue1)
                                      return BuiltinValue{};

                                  const Mat4& M = (*CheckedMathValue1);
                                  const int Index = IntegerArgument1;
                                  if (Index < 0 || Index > 15)
                                      return BuiltinValue::FromNumber(0.0);
                                  return BuiltinValue::FromNumber(static_cast<double>(M[Index]));
                              }}},
                            {"LENGTH VECTOR3",
                             {1, 1,
                              [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int, std::string& Error)
                              {
                                  int IntegerArgument0 = 0;
                                  if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                      return BuiltinValue{};
                                  auto* CheckedMathValue1 =
                                      RequireVector(Ctx.Vectors, IntegerArgument0, "LENGTH VECTOR3", &Error);
                                  if (!CheckedMathValue1)
                                      return BuiltinValue{};

                                  return BuiltinValue::FromNumber(static_cast<double>(VecLength((*CheckedMathValue1))));
                              }}},
                            {"DOT PRODUCT VECTOR3",
                             {2, 2,
                              [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int, std::string& Error)
                              {
                                  int IntegerArgument0 = 0;
                                  if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                      return BuiltinValue{};
                                  int IntegerArgument1 = 0;
                                  if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument1, &Error))
                                      return BuiltinValue{};
                                  auto* CheckedMathValue1 =
                                      RequireVector(Ctx.Vectors, IntegerArgument0, "DOT PRODUCT VECTOR3", &Error);
                                  if (!CheckedMathValue1)
                                      return BuiltinValue{};
                                  auto* CheckedMathValue2 =
                                      RequireVector(Ctx.Vectors, IntegerArgument1, "DOT PRODUCT VECTOR3", &Error);
                                  if (!CheckedMathValue2)
                                      return BuiltinValue{};

                                  return BuiltinValue::FromNumber(
                                      static_cast<double>(VecDot((*CheckedMathValue1), (*CheckedMathValue2))));
                              }}},
                            // Input: ESCAPEKEY, MOUSEX, MOUSEY, MOUSEZ
                            // Joystick / Gamepad queries (SDL3 Gamepad API)
                            // Light query functions
                            // Object collision sliding queries
                            // Misc system queries
                            {"CHECK DISPLAY MODE",
                             {3, 6,
                              [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int, std::string& Error)
                              {
                                  int IntegerArgument0 = 0;
                                  if (Args.size() > 3 && !Args[3].AsInt(IntegerArgument0, &Error))
                                      return BuiltinValue{};
                                  int IntegerArgument1 = 0;
                                  if (Args.size() > 4 && !Args[4].AsInt(IntegerArgument1, &Error))
                                      return BuiltinValue{};
                                  int IntegerArgument2 = 0;
                                  if (Args.size() > 5 && !Args[5].AsInt(IntegerArgument2, &Error))
                                      return BuiltinValue{};
                                  int IntegerArgument3 = 0;
                                  if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument3, &Error))
                                      return BuiltinValue{};
                                  int IntegerArgument4 = 0;
                                  if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument4, &Error))
                                      return BuiltinValue{};
                                  int IntegerArgument5 = 0;
                                  if (Args.size() > 2 && !Args[2].AsInt(IntegerArgument5, &Error))
                                      return BuiltinValue{};
                                  const int VSync = Args.size() >= 6 ? IntegerArgument0 : 1;
                                  const int Multisampling = Args.size() >= 6 ? IntegerArgument1 : 0;
                                  const int Multimonitor = Args.size() >= 6 ? IntegerArgument2 : 0;
                                  return BuiltinValue::FromNumber(CheckDisplayMode(Ctx, IntegerArgument3,
                                                                                   IntegerArgument4, IntegerArgument5,
                                                                                   VSync, Multisampling, Multimonitor));
                              }}},
                            // Camera angle query aliases
                            {"LIMB SCALE Z",
                             {2, 2,
                              [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int, std::string& Error)
                              {
                                  int IntegerArgument0 = 0;
                                  if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                      return BuiltinValue{};
                                  int IntegerArgument1 = 0;
                                  if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument1, &Error))
                                      return BuiltinValue{};
                                  return BuiltinValue::FromNumber(static_cast<double>(
                                      Ctx.Kernel->Objects().LimbScaleZ(IntegerArgument0, IntegerArgument1)));
                              }}},

                            // --- DBPro: Sprites (implemented) ---
                            {"SPRITE BLUE",
                             {1, 1,
                              [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int, std::string& Error)
                              {
                                  int IntegerArgument0 = 0;
                                  if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                      return BuiltinValue{};
                                  return ReadBuiltinNumber<float>(
                                      [&](float& Output)
                                      { return Ctx.Kernel->Sprites().SpriteBlue(Output, IntegerArgument0, &Error); });
                              }}},

                            // --- DBPro: 3D Objects — Picking ---

                            // --- DBPro: Terrain ---

                            // --- DBPro: Limbs/Mesh ---
                            {"CHECK LIMB LINK",
                             {2, 2,
                              [](BuiltinContext& Ctx, const std::vector<BuiltinValue>& Args, int, std::string& Error)
                              {
                                  int IntegerArgument0 = 0;
                                  if (Args.size() > 0 && !Args[0].AsInt(IntegerArgument0, &Error))
                                      return BuiltinValue{};
                                  int IntegerArgument1 = 0;
                                  if (Args.size() > 1 && !Args[1].AsInt(IntegerArgument1, &Error))
                                      return BuiltinValue{};
                                  return BuiltinValue::FromNumber(static_cast<double>(
                                      Ctx.Kernel->Objects().CheckLimbLink(IntegerArgument0, IntegerArgument1)));
                              }}},

                            // --- DBPro: Vertex Data ---

                            // --- DBPro: Particles ---

                            // --- DBPro: Shaders/Effects ---

                            // --- DBPro: Collision — BSP ---

                            // --- DBPro: Video ---

                            // --- DBPro: FTP ---
                        };
                return Table;
            }();
            return KFunctions;
        }

        template <typename Entry>
        using BindingTable = std::array<const Entry*, static_cast<std::size_t>(language::SymbolId::Count)>;

        template <typename Entry>
        BindingTable<Entry> BuildBindings(const ankerl::unordered_dense::map<std::string, Entry>& Entries,
                                          language::SymbolKind Kind)
        {
            BindingTable<Entry> Result{};
            for (const auto& [Name, EntryValue] : Entries)
            {
                const language::SymbolDescriptor& Descriptor = DescriptorForBinding(Kind, Name);
                if (Descriptor.ImplementedProfiles == 0)
                    continue;
                const language::BuiltinConvention Convention = language::ConventionFor(Descriptor.Id);
                if (Convention != language::BuiltinConvention::Values &&
                    Convention != language::BuiltinConvention::Print)
                    continue;
                const std::size_t Index = static_cast<std::size_t>(Descriptor.Id);
                assert(!Result[Index] && "Duplicate engine handler");
                Result[Index] = &EntryValue;
            }
            return Result;
        }

        const BindingTable<CommandEntry>& CommandBindings()
        {
            static const BindingTable<CommandEntry> Bindings = BuildBindings(Commands(), language::SymbolKind::Command);
            return Bindings;
        }

        const BindingTable<FunctionEntry>& FunctionBindings()
        {
            static const BindingTable<FunctionEntry> Bindings =
                BuildBindings(Functions(), language::SymbolKind::Function);
            return Bindings;
        }

    } // namespace

    bool HasBuiltinHandler(language::SymbolId Symbol)
    {
        const language::SymbolDescriptor& Descriptor = language::Symbol(Symbol);
        const std::size_t Index = static_cast<std::size_t>(Symbol);
        if (Descriptor.Kind == language::SymbolKind::Command)
            return CommandBindings()[Index] != nullptr;
        if (Descriptor.Kind == language::SymbolKind::Function)
            return FunctionBindings()[Index] != nullptr;
        return false;
    }

    bool InvokeCommand(BuiltinContext& Context, language::SymbolId Symbol, const std::vector<BuiltinValue>& Arguments,
                       std::string& Error)
    {
        Error.clear();
        const language::SymbolDescriptor& Descriptor = language::Symbol(Symbol);
        if (Descriptor.Kind != language::SymbolKind::Command)
        {
            Error = "Symbol is not a command: " + std::string(Descriptor.CanonicalName);
            return false;
        }
        if (!CheckInvocation(Descriptor, Context.Profile, Arguments.size(), Error))
            return false;
        const CommandEntry* Entry = CommandBindings()[static_cast<std::size_t>(Symbol)];
        if (!Entry)
        {
            Error = "Implemented command has no engine handler: " + std::string(Descriptor.CanonicalName);
            return false;
        }

        return Entry->Execute(Context, Arguments, 0, Error);
    }

    bool InvokeFunction(BuiltinContext& Context, language::SymbolId Symbol, const std::vector<BuiltinValue>& Arguments,
                        BuiltinValue& Result, std::string& Error)
    {
        Error.clear();
        const language::SymbolDescriptor& Descriptor = language::Symbol(Symbol);
        if (Descriptor.Kind != language::SymbolKind::Function)
        {
            Error = "Symbol is not a function: " + std::string(Descriptor.CanonicalName);
            return false;
        }
        if (!CheckInvocation(Descriptor, Context.Profile, Arguments.size(), Error))
            return false;
        const FunctionEntry* Entry = FunctionBindings()[static_cast<std::size_t>(Symbol)];
        if (!Entry)
        {
            Error = "Implemented function has no engine handler: " + std::string(Descriptor.CanonicalName);
            return false;
        }

        BuiltinValue Value = Entry->Execute(Context, Arguments, 0, Error);
        if (!Error.empty())
            return false;
        Result = std::move(Value);
        return true;
    }

} // namespace dbc::interpreter::detail
