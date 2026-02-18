namespace etch::tests {
void registerParseTests();
void registerTnfaTests();
void registerTdfaTests();
}  // namespace etch::tests

int main() {
    etch::tests::registerParseTests();
    etch::tests::registerTnfaTests();
    etch::tests::registerTdfaTests();
    return 0;
}
