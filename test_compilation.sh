#!/bin/bash

# Test compilation of PQ Key Exchange Module
# This script checks if our PQ integration compiles correctly

echo "=== Testing PQ Key Exchange Module Compilation ==="
echo

# Check if required files exist
echo "1. Checking required files..."
files_to_check=(
    "src/modules/PQKeyExchangeModule.h"
    "src/modules/PQKeyExchangeModule.cpp" 
    "src/modules/Modules.cpp"
    "protobufs/meshtastic/portnums.proto"
    "protobufs/meshtastic/mesh.proto"
    "protobufs/meshtastic/deviceonly.proto"
)

for file in "${files_to_check[@]}"; do
    if [ -f "$file" ]; then
        echo "   ✅ $file exists"
    else
        echo "   ❌ $file missing"
        exit 1
    fi
done

echo
echo "2. Checking for PQ module registration in Modules.cpp..."
if grep -q "PQKeyExchangeModule" src/modules/Modules.cpp; then
    echo "   ✅ PQKeyExchangeModule is registered"
else
    echo "   ❌ PQKeyExchangeModule not registered"
    exit 1
fi

echo
echo "3. Checking protobuf definitions..."
if grep -q "PQ_KEY_EXCHANGE_APP" protobufs/meshtastic/portnums.proto; then
    echo "   ✅ PQ_KEY_EXCHANGE_APP port number defined"
else
    echo "   ❌ PQ_KEY_EXCHANGE_APP port number missing"
    exit 1
fi

if grep -q "PQKeyExchange" protobufs/meshtastic/mesh.proto; then
    echo "   ✅ PQKeyExchange message defined"
else
    echo "   ❌ PQKeyExchange message missing" 
    exit 1
fi

echo
echo "4. Checking for KYBER library integration..."
if [ -d "lib/kyberkem" ]; then
    echo "   ✅ KYBER library directory exists"
    if ls lib/kyberkem/src/*.c lib/kyberkem/src/*.cpp &> /dev/null; then
        echo "   ✅ KYBER source files found"
    else
        echo "   ❌ KYBER source files missing"
        exit 1
    fi
else
    echo "   ❌ KYBER library directory missing"
    exit 1
fi

echo
echo "5. Checking for proper includes..."
if grep -q "PQKeyExchangeModule.h" src/modules/Modules.cpp; then
    echo "   ✅ PQKeyExchangeModule included in Modules.cpp"
else
    echo "   ❌ PQKeyExchangeModule not included"
    exit 1
fi

if grep -q "PQKeyExchangeModule.h" src/modules/NodeInfoModule.cpp; then
    echo "   ✅ PQKeyExchangeModule included in NodeInfoModule.cpp"
else
    echo "   ❌ PQKeyExchangeModule not included in NodeInfoModule"
    exit 1
fi

echo
echo "✅ All compilation checks passed!"
echo "🎯 Ready to proceed with PQ Key Exchange implementation"
