// What the Android Studio asks of its activity (tools/studio/android): the keyboard's height, what
// covers the window's edges and the Stop button over a running program.

#include "safe_area.hpp"
#include "screen_keyboard.hpp"
#include "stop_button.hpp"

#include <SDL3/SDL_system.h>
#include <SDL3/SDL_video.h>

#include <jni.h>

namespace dbc::studio
{
    namespace core
    {
        namespace
        {
            // The activity for the length of one call; SDL's references are local to the calling thread.
            struct Activity
            {
                JNIEnv* Env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
                jobject Object = Env != nullptr ? static_cast<jobject>(SDL_GetAndroidActivity()) : nullptr;
                jclass Class = Object != nullptr ? Env->GetObjectClass(Object) : nullptr;

                Activity() = default;
                Activity(const Activity&) = delete;
                Activity& operator=(const Activity&) = delete;

                ~Activity()
                {
                    if (Class != nullptr)
                        Env->DeleteLocalRef(Class);
                    if (Object != nullptr)
                        Env->DeleteLocalRef(Object);
                }

                // A method the activity lacks leaves an exception pending, which would end the next call.
                jmethodID Method(const char* Name, const char* Signature, bool Static) const
                {
                    if (Class == nullptr)
                        return nullptr;
                    const jmethodID Id = Static ? Env->GetStaticMethodID(Class, Name, Signature)
                                                : Env->GetMethodID(Class, Name, Signature);
                    if (Id == nullptr)
                        Env->ExceptionClear();
                    return Id;
                }

                void Call(const char* Name) const
                {
                    if (const jmethodID Id = Method(Name, "()V", false))
                        Env->CallVoidMethod(Object, Id);
                }
            };
        } // namespace

        float ScreenKeyboardHeight()
        {
            const Activity Host;
            const jmethodID Id = Host.Method("keyboardHeight", "()I", true);
            return Id != nullptr ? static_cast<float>(Host.Env->CallStaticIntMethod(Host.Class, Id)) : 0.0f;
        }

        bool WindowSafeArea(SDL_Window* Window, SDL_Rect& Area)
        {
            int Width = 0;
            int Height = 0;
            if (!SDL_GetWindowSize(Window, &Width, &Height))
                return false;
            const Activity Host;
            jint Insets[4] = {};
            if (const jmethodID Id = Host.Method("coveredEdges", "()[I", true))
            {
                const auto Edges = static_cast<jintArray>(Host.Env->CallStaticObjectMethod(Host.Class, Id));
                if (Edges != nullptr)
                {
                    if (Host.Env->GetArrayLength(Edges) == 4)
                        Host.Env->GetIntArrayRegion(Edges, 0, 4, Insets);
                    Host.Env->DeleteLocalRef(Edges);
                }
            }
            // Left, top, right, bottom.
            Area = SDL_Rect{Insets[0], Insets[1], Width - Insets[0] - Insets[2], Height - Insets[1] - Insets[3]};
            return true;
        }

        void ShowStopButton(SDL_Window*)
        {
            Activity().Call("showStopButton");
        }

        void HideStopButton()
        {
            Activity().Call("hideStopButton");
        }

        bool TakeStopRequest()
        {
            const Activity Host;
            const jmethodID Id = Host.Method("takeStopRequest", "()Z", true);
            return Id != nullptr && Host.Env->CallStaticBooleanMethod(Host.Class, Id) == JNI_TRUE;
        }
    } // namespace core

} // namespace dbc::studio
