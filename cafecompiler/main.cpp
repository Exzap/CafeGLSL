#include "gx2_definitions.h"
#include "CafeGLSLCompiler.h" // the public header

#include "tests.h"
#include "./libgfd/gfd.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>

void PrintUsage()
{
    std::cout << "Usage: shader_compiler [options]\n";
    std::cout << "Options:\n";
    std::cout << "  -ps <file>        : Pixel shader file (can be used multiple times to pack multiple shaders into .gsh file)\n";
    std::cout << "  -vs <file>        : Vertex shader file (can be used multiple times to pack multiple shaders into .gsh file)\n";
    std::cout << "  -o <file>         : Output path for .gsh file (default: no file is written)\n";
    std::cout << "  -t                : Run tests\n";
    std::cout << "  -v                : Verbose output (prints assembly and debug information)\n";
}

std::string ReadFile(const std::string &filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        std::cerr << "Failed to open file: " << filePath << "\n";
        exit(-1);
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

int wutStdErrCallback(struct _reent *r, void *, const char *data, int len)
{
    std::cout.write(data, len);
    return len;
}

const uint64_t HookStdErrToStdOut() {
    fflush(stderr);
#if defined(__WUT__)
    auto storedStdErr = stderr->_write;
    stderr->_write = wutStdErrCallback;
    return (uint64_t)storedStdErr;
#else
    // Save the current stderr file descriptor
    int stderrBackup = dup(fileno(stderr));
    if (stderrBackup == -1) {
        std::cerr << "Failed to backup stderr" << std::endl;
        return 1;
    }

    if (dup2(fileno(stdout), fileno(stderr)) == -1) {
        std::cerr << "Failed to redirect stderr" << std::endl;
        return 1;
    }
    return (uint64_t)stderrBackup;
#endif
}

const void RestoreStdErrHook(uint64_t stderrBackup) {
    fflush(stderr);
#if defined(__WUT__)
    stderr->_write = (int (*)(struct _reent *, void *, const char *, int))stderrBackup;
#else
    // Restore the original stderr file descriptor
    if (dup2((int)stderrBackup, fileno(stderr)) == -1) {
        std::cerr << "Failed to restore stderr" << std::endl;
    }

    // Close the backup file descriptor
    close((int)stderrBackup);
#endif
}


GX2PixelShader *CompilePixelShader(const std::string &shaderSource, const std::string &shaderFile, bool printAssembly)
{
    char infoLogBuffer[1024];
    std::cout << "Compiling pixel shader: " << shaderFile << "\n";
    uint64_t storedStdErr = printAssembly ? HookStdErrToStdOut() : 0;
    GX2PixelShader *ps = GLSL_CompilePixelShader(shaderSource.c_str(), infoLogBuffer, 1024, printAssembly ? GLSL_COMPILER_FLAG_GENERATE_DISASSEMBLY : GLSL_COMPILER_FLAG_NONE);
    if (printAssembly)
        RestoreStdErrHook(storedStdErr);

    if (!ps)
    {
        std::cerr << "Shader " << shaderFile << " failed to compile:\n";
        std::cerr << infoLogBuffer << "\n";
        exit(-2);
    }
    return ps;
}

GX2VertexShader *CompileVertexShader(const std::string &shaderSource, const std::string &shaderFile, bool printAssembly)
{
    char infoLogBuffer[1024];
    std::cout << "Compiling vertex shader: " << shaderFile << "\n";
    uint64_t storedStdErr = printAssembly ? HookStdErrToStdOut() : 0;
    GX2VertexShader *vs = GLSL_CompileVertexShader(shaderSource.c_str(), infoLogBuffer, 1024, printAssembly ? GLSL_COMPILER_FLAG_GENERATE_DISASSEMBLY : GLSL_COMPILER_FLAG_NONE);
    if (printAssembly)
        RestoreStdErrHook(storedStdErr);

    if (!vs)
    {
        std::cerr << "Shader " << shaderFile << " failed to compile:\n";
        std::cerr << "Error:\n"
                  << infoLogBuffer << "\n";
        exit(-2);
    }
    return vs;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        PrintUsage();
        return -1;
    }

    bool runTests = false;
    bool printAssembly = false;
    std::string outputPath = "";
    std::vector<std::pair<std::string, std::string>> shaders;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "-v") == 0)
        {
            printAssembly = true;
        }
        else if (strcmp(argv[i], "-t") == 0)
        {
            runTests = true;
        }
        else if (strcmp(argv[i], "-o") == 0)
        {
            if (i + 1 < argc)
            {
                outputPath = argv[i + 1];
                ++i;
            }
            else
            {
                std::cerr << "Missing file argument for -o\n";
                PrintUsage();
                return -1;
            }
        }
        else if (strcmp(argv[i], "-ps") == 0 || strcmp(argv[i], "-vs") == 0)
        {
            if (i + 1 < argc)
            {
                shaders.emplace_back(argv[i], argv[i + 1]);
                ++i;
            }
            else
            {
                std::cerr << "Missing file argument for " << argv[i] << "\n";
                PrintUsage();
                return -1;
            }
        }
        else
        {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            PrintUsage();
            return -1;
        }
    }

    if (runTests)
    {
        return RunTests();        
    }


    if (shaders.empty())
    {
        std::cerr << "No shaders specified.\n";
        PrintUsage();
        return -1;
    }

    if (!GLSL_Init())
    {
        std::cerr << "Failed to initialize GLSL compiler.\n";
        return -1;
    }

    bool serialize = !outputPath.empty();

    GFDFile gshFile = {};
    for (const auto &shader : shaders)
    {
        std::string shaderType = shader.first;
        std::string shaderFile = shader.second;
        std::string shaderSource = ReadFile(shaderFile);

        if (shaderType == "-ps")
        {
            GX2PixelShader *ps = CompilePixelShader(shaderSource, shaderFile, printAssembly);
            gshFile.pixelShaders.push_back(*ps);
        }
        else if (shaderType == "-vs")
        {
            GX2VertexShader *vs = CompileVertexShader(shaderSource, shaderFile, printAssembly);
            gshFile.vertexShaders.push_back(*vs);
        }
        else
        {
            std::cerr << "Unknown shader type: " << shaderType << "\n";
            PrintUsage();
            return -1;
        }
    }

    if (serialize && !writeFile(gshFile, outputPath))
    {
        std::cerr << "Failed to write output file: " << outputPath << "\n";
        return -1;
    }

    GLSL_Shutdown();
    return 0;
}
