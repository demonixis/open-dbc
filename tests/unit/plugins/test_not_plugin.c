/* A library without the OpenDBC plugin marker, which LOAD DLL refuses. */
#if defined(_WIN32)
__declspec(dllexport)
#endif
int answer(void)
{
    return 42;
}
