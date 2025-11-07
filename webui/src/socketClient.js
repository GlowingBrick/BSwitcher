// Unix Socket 客户端
export class SocketClient {
    constructor() {
        this.socketPath = '/dev/BSwitcher';
    }

    // 与后端通信
    async communicate(request) {
        try {
            const { exec } = await import('./ksu.js');

            // 转义JSON字符串用于shell命令
            const requestJson = JSON.stringify(request);
            const escapedJson = this.escapeForShell(requestJson);

            // 首先尝试使用 nc
            const ncCommand = `echo '${escapedJson}' | nc -U ${this.socketPath}`;
            
            const ncResult = await exec(ncCommand);

            if (ncResult.errno === 0 && ncResult.stdout) {
                try {
                    const response = JSON.parse(ncResult.stdout);
                    return response;
                } catch (parseError) {
                    throw new Error('无效的响应格式');
                }
            } else {
                // nc 失败，尝试 socket_send
                return await this.trysocket_send(escapedJson, request);  //不是所有的netcat都是openbsd netcat
            }
        } catch (error) {
            throw error;
        }
    }

    // 尝试使用内建的 socket_send 通信
    async trysocket_send(escapedJson, originalRequest) {
        try {
            const { exec } = await import('./ksu.js');
            
            const socatCommand = `echo '${escapedJson}' | socket_send ${this.socketPath}`;
            
            const socatResult = await exec(socatCommand);

            if (socatResult.errno === 0 && socatResult.stdout) {
                try {
                    const response = JSON.parse(socatResult.stdout);
                    return response;
                } catch (parseError) {
                    throw new Error('无效的响应格式');
                }
            } else {
                throw new Error(`所有通信方式都失败: nc和socat都无法连接`);
            }
        } catch (error) {
            throw new Error(`socket_send 通信失败: ${error.message}`);
        }
    }

    // 转义字符串用于shell命令
    escapeForShell(str) {
        return str.replace(/'/g, "'\\''")
        .replace(/\n/g, ' ')
        .replace(/\r/g, ' ');
    }

    // 读取调度配置
    async readScheduler() {
        const request = {
            target: 'scheduler',
            mode: 'read'
        };

        const response = await this.communicate(request);
        return response; // 直接返回完整的配置对象
    }

    // 写入调度配置
    async writeScheduler(config) {
        const request = {
            target: 'scheduler',
            mode: 'write',
            data: config
        };

        const response = await this.communicate(request);

        if (response.status === 'success') {
            return response;
        } else {
            throw new Error(response.message || '写入配置失败');
        }
    }

    // 读取应用列表
    async readAppList() {
        const request = {
            target: 'applist',
            mode: 'read'
        };

        const response = await this.communicate(request);
        return response; // 返回应用列表数组
    }
}
