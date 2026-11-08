using System.Text;
using System.Text.Json;
using CyRevision.Git;
using CyRevision.Security;

JsonSerializerOptions options = new(JsonSerializerDefaults.Web) { WriteIndented = true };
if (args[0] == "escaping")
{
    string[] values = Enumerable.Range(0,128).Select(i => ((char)i).ToString())
        .Concat(new[] { "é ☕ 😀 <&>+`\"'\\", "\u2028\u2029\uE000\uFFFF" }).ToArray();
    Console.WriteLine(JsonSerializer.Serialize(values.Select(value => new { value, encoded = JsonSerializer.Serialize(value, options) }), options));
}
else if (args[0] == "vectors")
{
    using var key = System.Security.Cryptography.ECDsa.Create(System.Security.Cryptography.ECCurve.NamedCurves.nistP256);
    var author = new DeviceIdentity(Guid.Parse("11111111-2222-4333-8444-555555555555"), Guid.Parse("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"),
        "Alice <é> + & ` \"😀\"\n", "", Convert.ToBase64String(key.ExportSubjectPublicKeyInfo()));
    var dates = new[] { "2026-11-08T05:06:07.1234567+00:00", "2024-02-29T23:59:59.1200000-05:30", "0001-01-01T00:00:00+00:00", "9999-12-31T23:59:59.9999999+00:00" };
    var vectors = dates.Select((date,index) => {
        var manifest = new GitPeerBundleManifest(Guid.Parse("00000001-0002-4003-8004-000000000005"), Guid.Parse("12345678-1234-4234-8234-123456789abc"), author,
            DateTimeOffset.Parse(date, System.Globalization.CultureInfo.InvariantCulture), "repository.bundle", new string('a',64),
            new Dictionary<string,string> { ["main"] = new string('b',40), ["feature/é+&"] = new string('c',40) }) { Sequence = index };
        byte[] payload = JsonSerializer.SerializeToUtf8Bytes(manifest,options);
        return new { document = new SignedGitPeerBundle(manifest,Convert.ToBase64String(key.SignData(payload,System.Security.Cryptography.HashAlgorithmName.SHA256))), canonical = Convert.ToBase64String(payload) };
    }).ToArray();
    Console.WriteLine(JsonSerializer.Serialize(vectors,options));
}
else if (args[0] == "inventory-vectors")
{
    using var key = System.Security.Cryptography.ECDsa.Create(System.Security.Cryptography.ECCurve.NamedCurves.nistP256);
    var device = new DeviceIdentity(Guid.NewGuid(), Guid.NewGuid(), "Reference é + &", "", Convert.ToBase64String(key.ExportSubjectPublicKeyInfo()));
    var inventory = new GitPeerContentInventory(Guid.NewGuid(), Guid.Parse("12345678-1234-4234-8234-123456789abc"), device,
        DateTimeOffset.Parse("2026-11-08T05:06:07.1234567+00:00"),
        new[] { new PeerLfsInventoryObject(new string('a',64), 12345678901, PeerLfsObjectPriority.Current, new[] { "assets/é+&.bin", "other.bin" }, true),
                new PeerLfsInventoryObject(new string('b',64), 0, PeerLfsObjectPriority.Archive, Array.Empty<string>(), false) });
    byte[] payload = JsonSerializer.SerializeToUtf8Bytes(inventory,options);
    Console.WriteLine(JsonSerializer.Serialize(new { document = new SignedGitPeerContentInventory(inventory,
        Convert.ToBase64String(key.SignData(payload,System.Security.Cryptography.HashAlgorithmName.SHA256))), canonical = Convert.ToBase64String(payload) },options));
}
else if (args[0] == "canonical")
{
    SignedGitPeerBundle signed = JsonSerializer.Deserialize<SignedGitPeerBundle>(File.ReadAllBytes(args[1]), options)!;
    var canonicalMethod = typeof(GitPeerExchangeService).GetMethod("CanonicalBytes", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Static)!
        .MakeGenericMethod(typeof(GitPeerBundleManifest));
    byte[] canonical = (byte[])canonicalMethod.Invoke(null, new object[] { signed.Manifest })!;
    var compatibleVerifier = typeof(GitPeerExchangeService).GetMethod("VerifyCanonicalSignature", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Static)?
        .MakeGenericMethod(typeof(GitPeerBundleManifest));
    bool verified = compatibleVerifier is null ? PeerExchangeCodec.VerifySignature(signed.Manifest.Author, canonical, signed.Signature) :
        (bool)compatibleVerifier.Invoke(null, new object[] { signed.Manifest.Author, signed.Manifest, signed.Signature })!;
    Console.WriteLine(JsonSerializer.Serialize(new {
        canonical = Convert.ToBase64String(canonical),
        verified
    }, options));
}
else if (args[0] == "export")
{
    using FileDeviceIdentityStore identity = await FileDeviceIdentityStore.OpenOrCreateAsync(args[4], args[5], "");
    GitPeerExportResult result = await new GitPeerExchangeService().ExportDetailedAsync(
        Guid.Parse(args[3]), args[1], args[2], identity, new[] { identity.Identity }, GitPeerExchangeOptions.Default);
    Console.WriteLine(JsonSerializer.Serialize(new { identity = identity.Identity, result }, options));
}
else if (args[0] == "import")
{
    DeviceIdentity[] devices = JsonSerializer.Deserialize<DeviceIdentity[]>(File.ReadAllBytes(args[5]), options)!;
    GitPeerExchangeResult result = await new GitPeerExchangeService().ImportAsync(Guid.Parse(args[3]), args[1], args[2], args[4], devices);
    Console.WriteLine(JsonSerializer.Serialize(result, options));
}
else throw new ArgumentException("Unknown fixture command");
