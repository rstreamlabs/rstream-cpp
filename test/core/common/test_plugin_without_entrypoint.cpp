// See LICENSE file in the project root for license information.

extern "C" {

#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
void rstream_test_plugin_without_entrypoint()
{
}
}
