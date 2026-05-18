import sys
import os
import json
from github import Github

def parseFile(path):
    with open(path, "r") as f:
        data = json.loads(f)
    for file in data["files"]:
        if file["name"].endswith(".bin"):
            return file["name"], file["bytes"]

if len(sys.argv) != 4:
    print(f"expected usage: {sys.argv[0]} <PR number> <path to old-manifests> <path to new-manifests>")
    sys.exit(1)

pr_number = int(sys.argv[1])

token = os.getenv("GITHUB_TOKEN")
if not token:
    raise EnvironmentError("GITHUB_TOKEN not found in environment.")

repo_name = os.getenv("GITHUB_REPOSITORY")  # "owner/repo"
if not repo_name:
    raise EnvironmentError("GITHUB_REPOSITORY not found in environment.")

oldFiles = sys.argv[2]
old = set(os.path.join(oldFiles, f) for f in os.listdir(oldFiles) if os.path.isfile(f))
newFiles = sys.argv[3]
new = set(os.path.join(newFiles, f) for f in os.listdir(newFiles) if os.path.isfile(f))

startMarkdown = "# Target Size Changes\n\n"
markdown = ""

newlyIntroduced = new - old
if len(newlyIntroduced) > 0:
    markdown += "## Newly Introduced Targets\n\n"
    # create a table
    markdown += "| File | Size |\n"
    markdown += "| ---- | ---- |\n"
    for f in newlyIntroduced:
        name, size = parseFile(f)
        markdown += f"| `{name}` | {size}b |\n"

# do not log removed targets
# PRs only run a small subset of builds, so removed targets are not meaningful
# since they are very likely to just be not ran in PR CI

both = old & new
degradations = []
improvements = []
for f in both:
    oldName, oldSize = parseFile(f)
    _, newSize = parseFile(f)
    if oldSize != newSize:
        if newSize < oldSize:
            improvements.append((oldName, oldSize, newSize))
        else:
            degradations.append((oldName, oldSize, newSize))

if len(degradations) > 0:
    markdown += "\n## Degradation\n\n"
    # create a table
    markdown += "| File | Difference | Old Size | New Size |\n"
    markdown += "| ---- | ---------- | -------- | -------- |\n"
    for oldName, oldSize, newSize in degradations:
        markdown += f"| `{oldName}` | **{oldSize - newSize}b** | {oldSize}b | {newSize}b |\n"

if len(improvements) > 0:
    markdown += "\n## Improvement\n\n"
    # create a table
    markdown += "| File | Difference | Old Size | New Size |\n"
    markdown += "| ---- | ---------- | -------- | -------- |\n"
    for oldName, oldSize, newSize in improvements:
        markdown += f"| `{oldName}` | **{oldSize - newSize}b** | {oldSize}b | {newSize}b |\n"

if len(markdown) == 0:
    markdown = "No changes in target sizes detected."

g = Github(token)
repo = g.get_repo(repo_name)
pr = repo.get_pull(pr_number)

existing_comment = None
for comment in pr.get_issue_comments():
    if comment.body.startswith(startMarkdown):
        existing_comment = comment
        break

final_markdown = startMarkdown + markdown

if existing_comment:
    existing_comment.edit(body=final_markdown)
else:
    pr.create_issue_comment(body=final_markdown)
