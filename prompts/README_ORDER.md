# MCP45HVX1 Industry-Readiness Hardening Prompt Chunks

Send these prompts to the coding agent one by one, in order. Each prompt tells the agent to complete only that chunk, commit, sync/push, and stop.

## Order

1. `00_mcp45hvx1_baseline_branch_agents.md`
2. `01_core_health_address_contracts.md`
3. `02_device_model_safety_api_contracts.md`
4. `03_esp_idf_cli_parity_bus_lifecycle.md`
5. `04_tests_fault_injection_ci_guards.md`
6. `05_docs_release_metadata_examples.md`
7. `06_hil_validation_tooling.md`
8. `07_final_integration_report_merge_gate.md`

## Strategy

- Prompt 00 sets the branch, AGENTS.md, baseline, and plan.
- Prompts 01-06 implement focused chunks.
- Prompt 07 performs adversarial final review and creates the comprehensive final report.
- Each chunk requires subagents, checks, commit, and sync.
- Do not let the coder skip hardware-validation honesty: no hardware claim without logs.
