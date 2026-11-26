using System.Diagnostics;

const string help = """
WSL Cleaner - read-only Windows disk inventory
Usage: WslCleaner [--json | --help]
  --json  Print inventory as JSON.
  --help  Show this help.
Reports fixed volumes and known WSL2 / Docker Desktop VHDX file lengths.
File length is not physical allocation or guest filesystem usage.
Does not start WSL/Docker, change settings, delete or compact files.
""";
const string unsupported = "Windows only; no inventory collected.";
var cases = new List<(string Name, string[] Args, int Code, string Out, string Err)>
{
    ("help", ["--help"], 0, help + "\n", ""),
    ("invalid option", ["--wat"], 2, "", "Unknown or conflicting arguments. Use --help.\n"),
    ("invalid JSON combination", ["--json", "--wat"], 2, "",
        "Unknown or conflicting arguments. Use --help.\n")
};
if (!OperatingSystem.IsWindows())
{
    cases.Add(("unsupported text", [], 1, "", unsupported + "\n"));
    cases.Add(("unsupported JSON", ["--json"], 1,
        "{\"status\":\"unsupported\",\"message\":\"Windows only; no inventory collected.\",\"volumes\":[],\"vhdx\":[],\"notes\":[]}\n", ""));
}

var failures = 0;
foreach (var test in cases)
{
    var actual = await Run(test.Args);
    if (actual == (test.Code, test.Out, test.Err))
        Console.WriteLine($"PASS {test.Name}");
    else
    {
        failures++;
        Console.WriteLine($"FAIL {test.Name}");
        Console.WriteLine($"  expected: {Describe((test.Code, test.Out, test.Err))}");
        Console.WriteLine($"  actual:   {Describe(actual)}");
    }
}
if (OperatingSystem.IsWindows())
    Console.WriteLine("SKIP unsupported text / JSON: requires a non-Windows host.");
Console.WriteLine($"{cases.Count - failures} passed, {failures} failed, {(OperatingSystem.IsWindows() ? 2 : 0)} skipped.");
return failures == 0 ? 0 : 1;

static async Task<(int Code, string Out, string Err)> Run(string[] args)
{
    // Exercise only the public executable, with the harness's matching .NET runtime.
    // ProjectReference builds and copies WslCleaner.dll beside this harness.
    var start = new ProcessStartInfo(Environment.GetEnvironmentVariable("DOTNET_HOST_PATH") ?? "dotnet")
    {
        RedirectStandardOutput = true,
        RedirectStandardError = true,
        UseShellExecute = false
    };
    start.ArgumentList.Add("exec");
    start.ArgumentList.Add("--runtimeconfig");
    start.ArgumentList.Add(Path.Combine(AppContext.BaseDirectory, "WslCleaner.Checks.runtimeconfig.json"));
    start.ArgumentList.Add(Path.Combine(AppContext.BaseDirectory, "WslCleaner.dll"));
    foreach (var arg in args) start.ArgumentList.Add(arg);
    using var process = Process.Start(start) ?? throw new InvalidOperationException("Cannot launch CLI.");
    var stdout = process.StandardOutput.ReadToEndAsync();
    var stderr = process.StandardError.ReadToEndAsync();
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
    try { await process.WaitForExitAsync(timeout.Token); }
    catch (OperationCanceledException)
    {
        process.Kill(entireProcessTree: true);
        throw new TimeoutException("CLI did not exit within 10 seconds.");
    }
    return (process.ExitCode, Normalize(await stdout), Normalize(await stderr));
}

static string Normalize(string value) => value.Replace("\r\n", "\n");
static string Describe((int Code, string Out, string Err) result) =>
    $"exit={result.Code}, stdout={System.Text.Json.JsonSerializer.Serialize(result.Out)}, stderr={System.Text.Json.JsonSerializer.Serialize(result.Err)}";
