#pragma once

#include "workflow_engine.hpp"
#include <filesystem>
#include <map>

namespace ben_gear {
namespace workflow {

/// 工作流模板库
class WorkflowTemplateLibrary {
public:
    /// 注册模板
    void register_template(const WorkflowDefinition& template_) {
        std::unique_lock lock(mutex_);
        templates_[template_.id] = template_;
        
        log::info_fmt("workflow template registered: id={}, name={}", 
                      template_.id, template_.name);
    }
    
    /// 从文件加载模板
    bool load_from_directory(const std::filesystem::path& dir) {
        if (!std::filesystem::exists(dir)) {
            log::warn_fmt("template directory not found: {}", dir.string());
            return false;
        }
        
        int count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.path().extension() == ".json") {
                try {
                    auto template_ = load_from_file(entry.path());
                    if (template_) {
                        register_template(*template_);
                        count++;
                    }
                } catch (const std::exception& e) {
                    log::error_fmt("failed to load template {}: {}", 
                                   entry.path().string(), e.what());
                }
            }
        }
        
        log::info_fmt("loaded {} workflow templates from {}", count, dir.string());
        return count > 0;
    }
    
    /// 查询模板
    std::optional<WorkflowDefinition> get(const std::string& name) const {
        std::shared_lock lock(mutex_);
        auto it = templates_.find(name);
        if (it != templates_.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    
    /// 列出所有模板
    /// 模板数量
    size_t size() const {
        std::shared_lock lock(mutex_);
        return templates_.size();
    }

    std::vector<std::string> list() const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> names;
        for (const auto& [name, _] : templates_) {
            names.push_back(name);
        }
        return names;
    }
    
    /// 模板实例化（替换变量）
    WorkflowDefinition instantiate(
        const std::string& name,
        const Json& variables) const {
        
        auto template_opt = get(name);
        if (!template_opt) {
            throw std::runtime_error("Template not found: " + name);
        }
        
        auto workflow = *template_opt;
        
        // 合并变量
        for (auto it = variables.begin(); it != variables.end(); ++it) {
            workflow.variables[it.key()] = it.value();
        }
        
        // 替换任务中的变量引用
        for (auto& task : workflow.tasks) {
            task.prompt = replace_variables(task.prompt, workflow.variables);
        }
        
        return workflow;
    }
    
private:
    /// 从文件加载模板
    std::optional<WorkflowDefinition> load_from_file(const std::filesystem::path& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return std::nullopt;
        }
        
        Json json;
        file >> json;
        
        WorkflowDefinition workflow;
        workflow.id = json.value("id", "");
        workflow.name = json.value("name", "");
        workflow.on_failure = json.value("on_failure", "abort");
        
        if (json.contains("variables")) {
            workflow.variables = json["variables"];
        }
        
        if (json.contains("tasks") && json["tasks"].is_array()) {
            for (const auto& task_json : json["tasks"]) {
                WorkflowTaskDefinition task;
                task.id = task_json.value("id", "");
                task.name = task_json.value("name", "");
                task.type = task_json.value("type", "function");
                task.prompt = task_json.value("prompt", "");
                
                if (task_json.contains("depends_on") && task_json["depends_on"].is_array()) {
                    for (const auto& dep : task_json["depends_on"]) {
                        task.depends_on.push_back(dep.get<std::string>());
                    }
                }
                
                if (task_json.contains("config")) {
                    task.config = task_json["config"];
                }
                
                workflow.tasks.push_back(task);
            }
        }
        
        return workflow;
    }
    
    /// 替换变量
    std::string replace_variables(const std::string& str, const Json& variables) const {
        std::string result = str;
        
        for (auto it = variables.begin(); it != variables.end(); ++it) {
            base::container::String placeholder = base::container::String("{") + it.key() + base::container::String("}");
            if (it.value().is_string()) {
                result = replace_all(result, placeholder, it.value().get<base::container::String>());
            }
        }
        
        return result;
    }
    
    std::string replace_all(const std::string& str, const std::string& from, const std::string& to) const {
        std::string result = str;
        size_t pos = 0;
        while ((pos = result.find(from, pos)) != std::string::npos) {
            result.replace(pos, from.length(), to);
            pos += to.length();
        }
        return result;
    }
    
private:
    std::map<std::string, WorkflowDefinition> templates_;
    mutable std::shared_mutex mutex_;
};

/// 内置模板工厂
namespace templates {

/// 代码审查模板
inline WorkflowDefinition code_review() {
    return {
        .id = "code_review",
        .name = "Code Review Workflow",
        .tasks = {
            {
                .id = "analyze_structure",
                .name = "Analyze Code Structure",
                .type = "llm",
                .prompt = "Analyze the code structure of {project_path}:\n"
                         "1. Directory structure and organization\n"
                         "2. Main modules and their responsibilities\n"
                         "3. Dependencies between modules\n"
                         "4. Code quality observations",
                .depends_on = {},
                .config = Json{{"timeout_seconds", 300}}
            },
            {
                .id = "check_style",
                .name = "Check Code Style",
                .type = "tool",
                .prompt = "run_command: find {project_path} -name '*.cpp' -o -name '*.hpp' | head -20",
                .depends_on = {},
                .config = Json{{"timeout_seconds", 60}}
            },
            {
                .id = "security_scan",
                .name = "Security Scan",
                .type = "llm",
                .prompt = "Scan for potential security issues in {project_path}:\n"
                         "1. SQL injection risks\n"
                         "2. Buffer overflow possibilities\n"
                         "3. Insecure dependencies\n"
                         "4. Sensitive data exposure",
                .depends_on = {},
                .config = Json{{"timeout_seconds", 300}}
            },
            {
                .id = "generate_report",
                .name = "Generate Review Report",
                .type = "llm",
                .prompt = "Generate a comprehensive code review report:\n\n"
                         "## Code Structure Analysis\n{analyze_structure}\n\n"
                         "## Style Check Results\n{check_style}\n\n"
                         "## Security Scan Results\n{security_scan}\n\n"
                         "## Summary & Recommendations\n"
                         "Provide actionable recommendations based on the above analysis.",
                .depends_on = {"analyze_structure", "check_style", "security_scan"},
                .config = Json{{"timeout_seconds", 300}}
            }
        },
        .variables = Json{{"project_path", ""}},
        .on_failure = "continue"
    };
}

/// 文档生成模板
inline WorkflowDefinition documentation() {
    return {
        .id = "documentation",
        .name = "Documentation Generation Workflow",
        .tasks = {
            {
                .id = "analyze_project",
                .name = "Analyze Project",
                .type = "llm",
                .prompt = "Analyze the project at {project_path}:\n"
                         "1. Project purpose and scope\n"
                         "2. Main components\n"
                         "3. Key features\n"
                         "4. Target audience",
                .depends_on = {},
                .config = Json{{"timeout_seconds", 300}}
            },
            {
                .id = "extract_api",
                .name = "Extract API Documentation",
                .type = "llm",
                .prompt = "Extract API documentation from {project_path}:\n"
                         "1. Public interfaces\n"
                         "2. Function signatures\n"
                         "3. Parameter descriptions\n"
                         "4. Return values and exceptions",
                .depends_on = {},
                .config = Json{{"timeout_seconds", 300}}
            },
            {
                .id = "analyze_examples",
                .name = "Analyze Examples",
                .type = "tool",
                .prompt = "list_dir: {project_path}/examples",
                .depends_on = {},
                .config = Json{{"recursive", true}}
            },
            {
                .id = "generate_readme",
                .name = "Generate README",
                .type = "llm",
                .prompt = "Generate a README.md for the project:\n\n"
                         "Project Analysis: {analyze_project}\n\n"
                         "API Overview: {extract_api}\n\n"
                         "Examples: {analyze_examples}\n\n"
                         "Include: Title, Description, Installation, Usage, API Reference, Examples, Contributing, License.",
                .depends_on = {"analyze_project", "extract_api", "analyze_examples"},
                .config = Json{{"timeout_seconds", 300}}
            }
        },
        .variables = Json{{"project_path", ""}},
        .on_failure = "continue"
    };
}

/// 重构辅助模板
inline WorkflowDefinition refactoring() {
    return {
        .id = "refactoring",
        .name = "Refactoring Assistant Workflow",
        .tasks = {
            {
                .id = "quality_analysis",
                .name = "Code Quality Analysis",
                .type = "llm",
                .prompt = "Analyze code quality issues in {target_path}:\n"
                         "1. Code smells (long methods, large classes, duplicate code)\n"
                         "2. Complexity metrics\n"
                         "3. Coupling issues\n"
                         "4. SOLID principle violations",
                .depends_on = {},
                .config = Json{{"timeout_seconds", 300}}
            },
            {
                .id = "dependency_analysis",
                .name = "Dependency Analysis",
                .type = "llm",
                .prompt = "Analyze dependencies in {target_path}:\n"
                         "1. Circular dependencies\n"
                         "2. Unused dependencies\n"
                         "3. Tight coupling\n"
                         "4. Suggested improvements",
                .depends_on = {},
                .config = Json{{"timeout_seconds", 300}}
            },
            {
                .id = "refactoring_suggestions",
                .name = "Generate Refactoring Suggestions",
                .type = "llm",
                .prompt = "Based on the analysis:\n\n"
                         "Quality Issues: {quality_analysis}\n\n"
                         "Dependency Issues: {dependency_analysis}\n\n"
                         "Generate specific refactoring suggestions:\n"
                         "1. Priority order (high to low impact)\n"
                         "2. Estimated effort for each\n"
                         "3. Risk assessment\n"
                         "4. Step-by-step implementation guide",
                .depends_on = {"quality_analysis", "dependency_analysis"},
                .config = Json{{"timeout_seconds", 300}}
            }
        },
        .variables = Json{{"target_path", ""}},
        .on_failure = "abort"
    };
}

/// 测试生成模板
inline WorkflowDefinition test_generation() {
    return {
        .id = "test_generation",
        .name = "Test Generation Workflow",
        .tasks = {
            {
                .id = "analyze_logic",
                .name = "Analyze Code Logic",
                .type = "llm",
                .prompt = "Analyze the code logic in {target_file}:\n"
                         "1. Main functions and their purposes\n"
                         "2. Input/output specifications\n"
                         "3. Edge cases and boundary conditions\n"
                         "4. Dependencies and mocking requirements",
                .depends_on = {},
                .config = Json{{"timeout_seconds", 300}}
            },
            {
                .id = "identify_scenarios",
                .name = "Identify Test Scenarios",
                .type = "llm",
                .prompt = "Based on the code analysis:\n\n{analyze_logic}\n\n"
                         "Identify comprehensive test scenarios:\n"
                         "1. Happy path tests\n"
                         "2. Edge case tests\n"
                         "3. Error handling tests\n"
                         "4. Performance tests (if applicable)\n"
                         "5. Integration tests",
                .depends_on = {"analyze_logic"},
                .config = Json{{"timeout_seconds", 300}}
            },
            {
                .id = "generate_unit_tests",
                .name = "Generate Unit Tests",
                .type = "llm",
                .prompt = "Generate unit tests for {target_file}:\n\n"
                         "Test Scenarios: {identify_scenarios}\n\n"
                         "Use the testing framework: {test_framework}\n"
                         "Include:\n"
                         "1. Test fixtures and setup\n"
                         "2. Test cases with assertions\n"
                         "3. Mock objects if needed\n"
                         "4. Comments explaining each test",
                .depends_on = {"identify_scenarios"},
                .config = Json{{"timeout_seconds", 300}}
            }
        },
        .variables = Json{
            {"target_file", ""},
            {"test_framework", "gtest"}
        },
        .on_failure = "abort"
    };
}

} // namespace templates

} // namespace workflow
} // namespace ben_gear
