#!/bin/bash
# Complete Validation Workflow
# Runs all validation steps in sequence

set -e

echo "========================================================================"
echo "MT5 VALIDATION COMPLETE WORKFLOW"
echo "========================================================================"
echo ""

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}Step 1: Verifying MT5 test data...${NC}"
python validation/verify_mt5_data.py
echo ""

echo -e "${BLUE}Step 2: Running complete analysis...${NC}"
python validation/analyze_all_tests.py
echo ""

echo -e "${GREEN}Validation workflow complete!${NC}"
echo ""
echo "Generated files:"
echo "  - include/mt5_validated_constants.h"
echo "  - validation/analysis/mt5_validated_config.json"
echo "  - validation/analysis/complete_analysis.json"
echo ""
echo "Next steps:"
echo "  1. Review: cat QUICK_START.md"
echo "  2. Integrate: Follow INTEGRATION_GUIDE.md"
echo "  3. Compare: python validation/compare_backtest_results.py"
echo ""
