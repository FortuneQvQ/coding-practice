let params=new URLSearchParams(window.location.search);
let from=params.get("from");


let html="";


if(from=="index"){

    html +=
    `
    <a class="button" href="../index.html">返回新闻主页</a>
    `;

}


else if(from=="category_result"){

    let title=params.get("title");
    let type=params.get("type");
    let value=params.get("value");
    let keyword=params.get("keyword");

    html +=
    `
    <a class="button" href="../category_result.html?title=${title}&type=${type}&value=${value}&keyword=${keyword}">返回分类结果</a>
    <a class="button" href="../category_index.html">返回分类主页</a>
    <a class="button" href="../index.html">返回新闻主页</a>
    `;
}


else if(from=="search"){
    
    let keyword=params.get("keyword");

    html +=
    `
    <a class="button" href="../search.html?keyword=${keyword}">返回搜索结果</a>
    <a class="button" href="../index.html">返回新闻主页</a>
    `;

}

document.getElementById("bottom-buttons").innerHTML = html;