function main(ctx)
    ctx:log("Starting long-running test job")
    for i = 1, 10 do
        os.execute("sleep 0.1")
    end
    ctx:log("Long-running test job finished")
end
