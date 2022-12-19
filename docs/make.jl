push!(LOAD_PATH, "../src/")
using Documenter

makedocs(sitename="Slimfly MPI collective optimization",
	 doctest = false,
	 pages=["Home"=> "index.md",
	 		"Slim Fly topology" => "slimfly.md",
			"InfiniBand Architecture" => "infiniband_architecture.md",
			"Routing" => "routing.md",
	 		"Benchmark" => "benchmarks.md",
			"Performance" => "performance.md",
			"Algorithm" => "algorithm.md",
			"MPI Tutorial" => "mpitutorial.md",
			"Trouble shooting" => "trouble_shooting.md",
			"Readings" => "readings.md"
		]
	 )

deploydocs(;
    repo="github.com/youwuyou/slimfly_collectives/"
)
