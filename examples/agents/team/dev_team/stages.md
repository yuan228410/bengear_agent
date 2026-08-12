# 工作阶段定义（| 分隔，# 开头为注释）
# 格式: stage_id | description | agent1,agent2 | dep1,dep2
# 串行：每个 stage 完成后才进入下一个

plan | 需求分析与方案设计 | planner |
code | 编码实现 | coder | plan
review | 代码审查 | reviewer | code
test | 测试验证 | tester | review
