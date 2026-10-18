// Writes the arguments of an indexed indirect draw of the number of indices a storage buffer holds, followed by those
// of a dispatch of one group.
StructuredBuffer<uint> counts : register(t0, space0);
RWStructuredBuffer<uint> arguments : register(u0, space1);

[numthreads(1, 1, 1)]
void main() {
    // DrawIndexedIndirectArguments: index count, instance count, first index, vertex offset, first instance.
    arguments[0] = counts[0];
    arguments[1] = 1;
    arguments[2] = 0;
    arguments[3] = 0;
    arguments[4] = 0;
    // DispatchIndirectArguments.
    arguments[5] = 1;
    arguments[6] = 1;
    arguments[7] = 1;
}
