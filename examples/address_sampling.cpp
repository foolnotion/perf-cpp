#include <perfcpp/sampler.h>
#include "access_benchmark.h"
#include <iostream>

struct alignas(64U) cache_line { std::int64_t value; };

int main()
{
    std::cout << "libperf-cpp example: Record perf samples including time, logical memory address, latency, and data source for single-threaded random access to an in-memory array." << std::endl;
    std::cout << "Note that this will work only on Intel CPUs that provide the counter `mem_trans_retired.load_latency_gt_X`." << std::endl;

    /// Initialize counter definitions.
    /// Note that the perf::CounterDefinition holds all counter names and must be alive until the benchmark finishes.
    auto counter_definitions = perf::CounterDefinition{};
    counter_definitions.add("mem_trans_retired.load_latency_gt_3", perf::CounterConfig{PERF_TYPE_RAW, 0x1CD, 0x3});

    /// Initialize sampler.
    auto perf_config = perf::SampleConfig{};
    perf_config.precise_ip(3U); /// precise_ip controls the amount of skid, see https://man7.org/linux/man-pages/man2/perf_event_open.2.html
    perf_config.period(1000000U); /// Record every 10000th event.

    auto sampler = perf::Sampler{
            counter_definitions,
            "mem_trans_retired.load_latency_gt_3", /// Event that generates an overflow which is samples (here we sample every 1,000,000th cycle)
            perf::Sampler::Type::Time | perf::Sampler::Type::LogicalMemAddress | perf::Sampler::Type::DataSource | perf::Sampler::Type::Weight, /// Controls what to include into the sample, see https://man7.org/linux/man-pages/man2/perf_event_open.2.html
            perf_config
    };

    /// Create random access benchmark.
    auto benchmark = perf::example::AccessBenchmark{/*randomize the accesses*/ true, /* create benchmark of 512 MB */ 512U};

    /// Start sampling.
    if (!sampler.start())
    {
        std::cerr << "Could not start sampling, errno = " << sampler.last_error() << "." << std::endl;
        return 1;
    }

    /// Execute the benchmark (accessing cache lines in a random order).
    auto value = 0ULL;
    for (auto index = 0U; index < benchmark.size(); ++index)
    {
        value += benchmark[index].value;
    }
    asm volatile("" : "+r,m"(value) : : "memory"); /// We do not want the compiler to optimize away this unused value.

    /// Stop sampling.
    sampler.stop();

    /// Get all the recorded samples.
    const auto samples = sampler.result();

    /// Print the first samples.
    const auto count_show_samples = std::min<std::size_t>(samples.size(), 40U);
    std::cout << "\nRecorded " << samples.size() << " samples." << std::endl;
    std::cout << "Here are the first " << count_show_samples << " recorded samples:\n" << std::endl;
    for (auto index = 0U; index < count_show_samples; ++index)
    {
        const auto &sample = samples[index];

        /// Since we recorded the time, period, the instruction pointer, and the CPU id, we can only read these values.
        if (sample.time().has_value() && sample.logical_memory_address().has_value() && sample.data_src().has_value())
        {
            auto data_source = "N/A";
            if (sample.data_src()->is_mem_hit())
            {
                data_source = "Mem";
            }
            else if (sample.data_src()->is_mem_l1())
            {
                data_source = "L1d";
            }
            else if (sample.data_src()->is_mem_lfb())
            {
                data_source = "LFB";
            }
            else if (sample.data_src()->is_mem_l2())
            {
                data_source = "L2";
            }
            else if (sample.data_src()->is_mem_l3())
            {
                data_source = "L3";
            }

            std::cout
                    << "Time = " << sample.time().value()
                    << " | Logical Mem Address = 0x" << std::hex << sample.logical_memory_address().value() << std::dec
                    << " | Load Latency = " << sample.weight().value()
                    << " | Is Load = " << sample.data_src()->is_load()
                    << " | Data Source = " << data_source
                << "\n";
        }
    }
    std::cout << std::flush;

    /// Close the sampler.
    /// Note that the sampler can only be closed after reading the samples.
    sampler.close();

    return 0;
}