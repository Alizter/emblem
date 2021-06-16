#!/bin/bash

func_checks=$(./scripts/yq '.deps + .check_deps | .[] | "  func_checks[\"" + .name + "\"] = \"" + .func_check + "\""' em.yml | sed 's/^../\t/')

perl -p0e "s/FUNC_CHECKS/$func_checks/s" < /dev/stdin > /dev/stdout
