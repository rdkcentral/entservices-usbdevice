# 🔧 Coverity Native Build System for entservices-usbdevice

The documentation and source for the RDK-B native build system has been centralized in [rdkcentral/build_tools_workflows](https://github.com/rdkcentral/build_tools_workflows/blob/develop/cov_docker_script/README.md)

## Quick Start

```bash
# Navigate to component root
cd /path/to/entservices-usbdevice

# Initialize/update build_tools_workflows submodule
git submodule update --init --recursive --remote

# Step 1: Setup dependencies
./build_tools_workflows/cov_docker_script/setup_dependencies.sh ./cov_docker_script/component_config.json

# Step 2: Build component
./build_tools_workflows/cov_docker_script/build_native.sh ./cov_docker_script/component_config.json "$(pwd)"

# Clean build (removes previous artifacts)
CLEAN_BUILD=true ./build_tools_workflows/cov_docker_script/setup_dependencies.sh ./cov_docker_script/component_config.json
./build_tools_workflows/cov_docker_script/build_native.sh ./cov_docker_script/component_config.json "$(pwd)"
```

## Component-Specific Configuration

This component uses:
- **Build System**: CMake with Ninja generator
- **Thunder Version**: R4.4 (Thunder R4.4.1, ThunderTools R4.4.3)
- **Key Dependencies**: 
  - Thunder & ThunderTools (WPEFramework)
  - entservices-apis
  - entservices-testframework (requires GitHub token)
  - trower-base64

## GitHub Token Requirement

The `entservices-testframework` repository requires authentication. When running locally, set the `GITHUB_TOKEN` environment variable:

```bash
export GITHUB_TOKEN=your_github_token
./build_tools_workflows/cov_docker_script/setup_dependencies.sh ./cov_docker_script/component_config.json
```

In CI/CD, the token is automatically provided via GitHub Actions secrets.

## Configuration Files

- **component_config.json**: Defines all dependencies, build configuration, and mock header setup
- Component uses CMake build system with extensive mock framework for testing

## References

- [Build Tools Workflows Documentation](https://github.com/rdkcentral/build_tools_workflows/blob/develop/cov_docker_script/README.md)
- [Native Build Workflow](.github/workflows/native-build.yml)
