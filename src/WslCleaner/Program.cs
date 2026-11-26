using System.Globalization;
using System.Runtime.Versioning;
using System.Security;
using System.Text.Json;
using Microsoft.Win32;

const string help = """
WSL Cleaner - read-only Windows disk inventory
Usage: WslCleaner [--json | --help]
  --json  Print inventory as JSON.
  --help  Show this help.
Reports fixed volumes and known WSL2 / Docker Desktop VHDX file lengths.
File length is not physical allocation or guest filesystem usage.
Does not start WSL/Docker, change settings, delete or compact files.
""";
if (args is ["--help"])
{
    Console.WriteLine(help);
    return 0;
}
if (args.Length != 0 && args is not ["--json"])
{
    Console.Error.WriteLine("Unknown or conflicting arguments. Use --help.");
    return 2;
}
var json = args.Length == 1;
if (!OperatingSystem.IsWindows())
{
    const string message = "Windows only; no inventory collected.";
    if (json) PrintJson(new Report("unsupported", message, [], [], []));
    else Console.Error.WriteLine(message);
    return 1;
}
var report = Inventory.Collect();
if (json) PrintJson(report);
else
{
    Console.WriteLine("WSL Cleaner - read-only snapshot");
    foreach (var volume in report.Volumes)
        Console.WriteLine($"Volume {volume.Path}: total {Bytes(volume.TotalBytes)}, free {Bytes(volume.FreeBytes)}; {volume.Error ?? "ok"}");
    foreach (var disk in report.Vhdx)
    {
        Console.WriteLine($"{disk.Kind}: {string.Join("; ", disk.Labels)}");
        Console.WriteLine($"  {disk.Path}");
        Console.WriteLine($"  VHDX file length: {Bytes(disk.LengthBytes)}; {disk.Status}{(disk.Error is null ? "" : ": " + disk.Error)}");
    }
    foreach (var note in report.Notes) Console.WriteLine($"Note: {note}");
    Console.WriteLine($"Status: {report.Status}. File length is not physical allocation or guest usage; do not add it to volume usage.");
}
return report.Status == "ok" ? 0 : 1;

static void PrintJson(Report value) => Console.WriteLine(JsonSerializer.Serialize(value,
    new JsonSerializerOptions { PropertyNamingPolicy = JsonNamingPolicy.CamelCase }));
static string Bytes(long? value) => value is long bytes
    ? bytes.ToString(CultureInfo.InvariantCulture) + " bytes" : "unknown";

record Report(string Status, string Message, List<Volume> Volumes, List<Disk> Vhdx, List<string> Notes);
record Volume(string Path, long? TotalBytes, long? FreeBytes, string? Error);
sealed class Disk(string path, string kind, string label)
{
    public string Path { get; } = path;
    public string Kind { get; set; } = kind;
    public List<string> Labels { get; } = [label];
    public long? LengthBytes { get; set; }
    public string Status { get; set; } = "unknown";
    public string? Error { get; set; }
}

[SupportedOSPlatform("windows")]
static class Inventory
{
    public static Report Collect()
    {
        var volumes = new List<Volume>();
        var disks = new Dictionary<string, Disk>(StringComparer.OrdinalIgnoreCase);
        var notes = new List<string>();
        var partial = false;
        void Failure(string context, Exception error)
        {
            partial = true;
            notes.Add($"{context}: {error.GetType().Name}: {error.Message}");
        }
        void AddDisk(string path, string kind, string label, bool expected)
        {
            try
            {
                // WSL commonly stores extended-length drive paths. Do not probe UNC paths.
                if (path.StartsWith(@"\\?\", StringComparison.Ordinal)) path = path[4..];
                if (!Path.IsPathFullyQualified(path) || path.Length < 3 || path[1] != ':' || path[2] != '\\')
                    throw new NotSupportedException("Only absolute local drive paths are supported.");
                path = Path.GetFullPath(path);
                if (new DriveInfo(Path.GetPathRoot(path)!).DriveType != DriveType.Fixed)
                    throw new NotSupportedException("Only fixed local drives are supported.");
                if (disks.TryGetValue(path, out var existing))
                {
                    if (!existing.Labels.Contains(label)) existing.Labels.Add(label);
                    if (kind == "Docker") existing.Kind = kind;
                    return;
                }
                var disk = new Disk(path, kind, label);
                disks.Add(path, disk);
                try
                {
                    disk.LengthBytes = new FileInfo(path).Length;
                    disk.Status = "measured";
                }
                catch (Exception error) when (ReadableFailure(error))
                {
                    var missing = error is FileNotFoundException or DirectoryNotFoundException;
                    disk.Status = missing ? "missing" : "unavailable";
                    disk.Error = error.GetType().Name + ": " + error.Message;
                    if (expected || !missing) partial = true;
                }
            }
            catch (Exception error) when (ReadableFailure(error)) { Failure(label, error); }
        }

        try
        {
            foreach (var drive in DriveInfo.GetDrives().OrderBy(d => d.Name, StringComparer.OrdinalIgnoreCase))
            {
                try
                {
                    if (drive.DriveType != DriveType.Fixed) continue;
                    volumes.Add(new Volume(drive.Name, drive.TotalSize, drive.TotalFreeSpace, null));
                }
                catch (Exception error) when (ReadableFailure(error))
                {
                    volumes.Add(new Volume(drive.Name, null, null, error.GetType().Name + ": " + error.Message));
                    partial = true;
                }
            }
        }
        catch (Exception error) when (ReadableFailure(error)) { Failure("Volume enumeration", error); }

        try
        {
            using var lxss = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Lxss", writable: false);
            if (lxss is null) notes.Add("No WSL registrations found for the current Windows user.");
            else foreach (var id in lxss.GetSubKeyNames().Order(StringComparer.OrdinalIgnoreCase))
            {
                try
                {
                    using var distro = lxss.OpenSubKey(id, writable: false);
                    if (distro is null) { partial = true; notes.Add($"WSL registration {id}: unavailable."); continue; }
                    var name = distro.GetValue("DistributionName") as string ?? id;
                    if (distro.GetValue("Version") is not int version)
                    { partial = true; notes.Add($"WSL {name}: version unknown; not probed."); continue; }
                    if (version != 2) { notes.Add($"WSL {name}: version {version}; no WSL2 VHDX measurement."); continue; }
                    if (distro.GetValue("BasePath") is not string basePath)
                    { partial = true; notes.Add($"WSL {name}: BasePath unavailable."); continue; }
                    var file = distro.GetValue("VhdFileName") as string ?? "ext4.vhdx";
                    // An unexpected registry value is not permission to scan other directories.
                    if (file.Length == 0 || file != Path.GetFileName(file) || !file.EndsWith(".vhdx", StringComparison.OrdinalIgnoreCase))
                        throw new NotSupportedException("Unsupported WSL VhdFileName.");
                    var docker = name.Equals("docker-desktop", StringComparison.OrdinalIgnoreCase)
                        || name.Equals("docker-desktop-data", StringComparison.OrdinalIgnoreCase);
                    AddDisk(Path.Combine(basePath, file), docker ? "Docker" : "WSL2", $"WSL registration: {name}", true);
                }
                catch (Exception error) when (ReadableFailure(error)) { Failure($"WSL registration {id}", error); }
            }
        }
        catch (Exception error) when (ReadableFailure(error)) { Failure("WSL registry", error); }

        var local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        if (string.IsNullOrEmpty(local)) { partial = true; notes.Add("LocalApplicationData unavailable; Docker locations not probed."); }
        else
        {
            AddDisk(Path.Combine(local, @"Docker\wsl\disk\docker_data.vhdx"), "Docker", "Docker Desktop modern data candidate", false);
            AddDisk(Path.Combine(local, @"Docker\wsl\data\ext4.vhdx"), "Docker", "Docker Desktop legacy data candidate", false);
            AddDisk(Path.Combine(local, @"Docker\wsl\distro\ext4.vhdx"), "Docker", "Docker Desktop legacy engine candidate", false);
        }
        notes.Add("Docker discovery covers current-user WSL registrations and known default Desktop locations only. Custom/moved disks, Hyper-V backend, other users and remote engines are unsupported/unknown; missing candidates do not prove Docker is absent.");
        notes.Add("VHDX length is the file's logical length, not physical allocation, maximum virtual capacity, guest usage or reclaimable space. No usage totals are inferred.");
        notes.Add("Paths are deduplicated case-insensitively after normalization; hard links, junction aliases and volume mount-point aliases are not resolved. Measurements are a non-atomic snapshot.");
        return new Report(partial ? "partial" : "ok", "Read-only Windows inventory.", volumes,
            disks.Values.OrderBy(d => d.Path, StringComparer.OrdinalIgnoreCase).ToList(), notes);
    }

    static bool ReadableFailure(Exception error) => error is IOException or UnauthorizedAccessException
        or SecurityException or ArgumentException or NotSupportedException;
}
