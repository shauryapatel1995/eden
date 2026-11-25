CALL gds.graph.project('baidu', 'Page', 'LINK') YIELD nodeCount, relationshipCount, projectMillis RETURN nodeCount, relationshipCount, projectMillis;
CALL gds.pageRank.stats('baidu', {maxIterations: 20, dampingFactor: 0.85}) YIELD ranIterations, didConverge, computeMillis, centralityDistribution RETURN ranIterations, didConverge, computeMillis, centralityDistribution.mean AS mean, centralityDistribution.max AS max, centralityDistribution.p99 AS p99;
CALL gds.graph.drop('baidu') YIELD graphName RETURN graphName;
