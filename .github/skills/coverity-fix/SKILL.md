---
# Fill in the fields below to create a basic custom agent for your repository.
# The Copilot CLI can be used for local testing: https://gh.io/customagents/cli
# To make this agent available, merge this file into the default repository branch.
# For format details, see: https://gh.io/customagents/config

name: coverity-fix
description: Use this to fix discovered static analysis / Coverity issues over selected components in the entservices repo. Use this only after a scan has completed and a master report has been generated for the scanned components.
---

# Coverity Fix Skill

To perform the Coverity fix, consider the following instructions. As a Coverity remediation lead for RDK, know that every instruction below is **mandatory** and must be enforced exactly.

The CSV already contains the canonical `Component Name` and `File Path` columns. Treat those workspace-relative paths as the source of truth.

----------------------------------------------------------------------
  **MASTER INSTRUCTIONS — ZERO DEVIATION**

  1. **STRICT FULL COMPONENT COVERAGE**
     1. If user specified a branch, switch to it; otherwise, switch to the latest branch `copilot/coverity-analysis-{YYYY_MM_DD-HH.MM.SS}` of the repo and read the entire CSV located at `.github/agent_output/reports/{repoName}_workspace_static_analysis.csv`. You will be using this same branch for the entire fix phase.
     2. Extract every component name in the exact order listed.
     3. List all components in order they appear in CSV
     4. Count components, display the ordered list, and confirm the total.
     5. For each component, enumerate the distinct workspace-relative file paths exactly as recorded in the CSV `File Path` column
     6. If a component has no issues, explicitly output: `Component <name>: No Coverity issues found.`
     7. Before proceeding to the next step, confirm that the listed components and file paths in the CSV match the actual components and file paths in the repository; do not proceed with the remaining instructions if there are any discrepancies.
     8. If the check in step 7 succeeds, add a new column to the CSV named `Resolution Status and Notes` for use in the next steps.

  2. **PROCESSING ONE COMPONENT AT A TIME**
     1. Select first component from the list
     2. Analyze all files for the component completely so that you understand the context of the component when performing fixes in the next step; do not skip helpers, headers, or platform variants.
     3. Extract all rows from CSV where Component Name matches
     4. For each row, you will:
       - Examine the entire file at File Path
       - Locate the exact line or inclusive line range recorded in Line Number
       - Review Source Code Snippet and Issue Description
       - Understand the identified issue. Identify if it is a false positive, if the suggested fix can be implemented, or if an alternative fix is needed.
         - If the issue is a false positive, mark the issue as 'False Positive' with a brief justification in the column `Resolution Status and Notes`
         - If an alternative fix is needed, implement it then mark the issue as 'Alternate Fix' with a brief justification in the column `Resolution Status and Notes`
         - Otherwise, implement the Suggested Fix
     5. Before moving to the next component, self-verify that every CSV row for the current component is either fixed or marked as a false positive, and that the per-component counts reconcile exactly.
     6. Do not move to next component until current component is 100% complete
     7. **Repeat for all components in sequence**

  3. **COVERITY FIXING RULES**
     1. **Preserve Public APIs:**
       - Do not change public functions' signatures, parameter types, or return types
       - Do not modify public struct/class definitions
       - Do not alter include guards or exported symbols
       - Fix internal logic only

     2. **Code Marking:**
       - For all edits include comments:
         // FIX(Issue <Row ID Number>): <issue type>
         // Reason: <short justification>
         // Impact: <short description of issue and how this fix resolves the issue>

     3. **No Massive Rewrites:**
       - Make minimum necessary changes to fix the issue
       - Preserve existing code style and variable names
       - Avoid rewriting entire functions unless absolutely necessary

     4. **Forbidden Operations:**
       - Do not create automation scripts or Python tools
       - Do not proceed without explicitly reading source files first
       - Do not skip components
       - Do not merge components
       - Do not skip issues within a component
       - Do not switch to a different branch; use the same branch specified in Instruction 1 for the entire fix phase
       - Do not change public APIs

  4. **REQUIRED OUTPUT PER COMPONENT**

     After fixing all issues in a component, output:

     ```
     === COMPONENT: [Component Name] ===

     ISSUE SUMMARY:
     - Total issues in component: [N]
     - Issues fixed: [N]
     - False positives (skipped): [N]

     UPDATED CODE:
     [For each fixed issue, show the updated code snippet with FIX comments]

     COVERAGE CONFIRMATION:
     All issues in [Component Name] have been addressed. ✓
     ```

  5. **FALSE POSITIVE & RESOLUTION STATS (MANDATORY)**
     - After processing all components, provide a table:

       ```
       | Status | Count |
       |--------|-------|
       | Total Issues Found | [N] |
       | Actually Resolved | [N] |
       | False Positives | [N] |
       ```

  6. **END-TO-END COMPLETION CHECK**
     1. Verify each component from CSV has been processed
     2. Verify all false positives or non-reproducible issues are documented
     3. Declare: *"All [N] components processed. [X] issues fixed, [Y] false positives. Ready to push changes."*
     4. Once the end-to-end completion check is complete, perform a final review of all changes made with Copilot code review tools, ensuring fixes do not introduce new issues and adhere to coding standards.

  7. **ABSOLUTELY FORBIDDEN**
     - Skipping or merging components
     - Skipping any issue within a component
     - Proceeding without reading the actual source file
     - Switching remediation to a different analysis branch after processing has started
     - Changing public interfaces
     - Massive rewrites or speculative fixes
     - Creating automation scripts or external tooling

  8. **PUSH CHANGES AND CREATE PR**
     1. Commit all fixes to the branch with message: `"Fix Coverity issues - [N] issues resolved"`
     2. Create a Pull Request from the branch to `develop`
     3. PR Title: `"Coverity Analysis and Fix - {N} issues resolved"`
     4. PR Body must include:
        - Summary of components fixed
        - Count of issues resolved vs false positives
        - Link to the related analysis branch
     5. Announce PR creation and await final review